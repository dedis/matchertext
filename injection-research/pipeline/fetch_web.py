"""Stage 1c: recover payloads from Packet Storm and seclists advisory pages.

These are the last sizeable PoC sources that host the advisory *text* rather
than another link to it, and unlike the commercial vulnerability databases they
need no API key. Together they cover roughly 2,200 injection CVEs that have no
local exploit file and no usable GitHub repo.

Both allow this in robots.txt -- Packet Storm disallows only /tos/, seclists only
its search pages -- but seclists states plainly that "abusive IPs which make
hundreds of requests in a short period of time will be banned". The rate limit
below is therefore a condition of access, not politeness: one request per second
per *site*, with different sites running concurrently so the throttle costs
nothing in wall clock.

Per site, not per hostname. An earlier version keyed the throttle on the
hostname, which gave packetstormsecurity.com and packetstormsecurity.org their
own budgets although they are one server; the resulting 2/s got us blocked.
Packet Storm serves that block as HTTP 200 with a refusal page, so it was
recorded as "no payload here" for a thousand advisories -- wrong data wearing
the shape of a result. Hence _BLOCKED, and a circuit breaker that abandons a
site after BLOCK_LIMIT consecutive refusals rather than continuing to hammer it.

Reproducibility follows the same principle as the GitHub pins, adapted to pages
that carry no commit id: the sha256 of each fetched page is stored, so a later
run can tell a changed advisory from an unchanged one.

Only the extracted payload is kept, never the page.

Usage:
  python3 pipeline/fetch_web.py            # full run, resumable
  python3 pipeline/fetch_web.py -n 50      # sample, for measurement
"""
import argparse
import hashlib
import html
import queue
import re
import sqlite3
import sys
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from report import extract_payload
import remote_sidecar

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"

HOSTS = {
    "packetstormsecurity.com": "packetstorm",
    "packetstormsecurity.org": "packetstorm",
    "seclists.org": "seclists",
}
UA = ("Mozilla/5.0 (compatible; matchertext-research/1.0; "
      "academic study of injection payload structure)")
TIMEOUT = 25

DDL = remote_sidecar.WEB_CREATE_DDL


def finish(con):
    digest = remote_sidecar.export(con)
    print(f"remote payload sidecar: {digest}")
    con.close()

_TAG = re.compile(r"<(script|style)\b[^>]*>.*?</\1>", re.I | re.S)
_ANY_TAG = re.compile(r"<[^>]+>")
_CODE = re.compile(r"<code\b[^>]*>(.*?)</code>", re.I | re.S)
_HOST = re.compile(r"https?://(?:www\.)?([^/]+)", re.I)
# A block is served as HTTP 200 with a refusal page, so status codes alone do
# not reveal it. Without this check a ban is silently recorded as "this advisory
# contains no payload", which is worse than an error: it is wrong data that looks
# like a finding.
_BLOCKED = re.compile(
    r"velocity rate limiting|detected misuse|\bbanned\b|access denied"
    r"|rate limit exceeded|too many requests|temporarily blocked", re.I)
# Consecutive blocks after which a site is abandoned for the rest of the run.
BLOCK_LIMIT = 3
GHSL_SITEMAP = "https://securitylab.github.com/sitemap.xml"
GHSL_URL = re.compile(r"<loc>(https://securitylab\.github\.com/advisories/[^<]+)</loc>")
CVE_RE = re.compile(r"CVE-\d{4}-\d+", re.I)


class Throttle:
    """One request per interval per host, so concurrency across hosts is free."""

    def __init__(self, interval):
        self.interval = interval
        self._locks = {}
        self._next = {}
        self._guard = threading.Lock()

    def wait(self, host):
        with self._guard:
            lock = self._locks.setdefault(host, threading.Lock())
        with lock:
            now = time.monotonic()
            due = self._next.get(host, 0.0)
            if now < due:
                time.sleep(due - now)
            self._next[host] = max(now, due) + self.interval


def host_of(url):
    m = _HOST.match(url.strip())
    return m.group(1).lower() if m else None


def candidates(con, done):
    """One page per CVE: the first advisory URL on a host we can read."""
    rows = con.execute("""
        SELECT cl.cve_id, cl.syntax_type, p.ref
        FROM classification cl JOIN poc p USING(cve_id)
        WHERE cl.cve_id NOT IN (SELECT cve_id FROM syntactic_group)
        ORDER BY cl.cve_id, p.ref""").fetchall()
    out = {}
    for cve, syn, ref in rows:
        if cve in done or cve in out:
            continue
        h = host_of(ref)
        if h in HOSTS:
            # Keyed on the site, not the hostname: packetstormsecurity.com and
            # .org are one server, and keying on the hostname handed it two
            # independent rate budgets, doubling the request rate and getting
            # us blocked.
            out[cve] = (syn, ref, HOSTS[h], HOSTS[h])
    return [(k, *out[k]) for k in sorted(out)]


def to_text(raw):
    """Advisory pages wrap the exploit in markup; recover the plain text.

    html.unescape rather than a hand-rolled entity table: an earlier ad-hoc
    version missed &apos;, which silently truncated every payload containing a
    single quote -- the majority of SQL injections.
    """
    body = _TAG.sub(" ", raw)
    return html.unescape(_ANY_TAG.sub(" ", body))


def code_text(raw):
    return "\n".join(html.unescape(_ANY_TAG.sub(" ", part))
                     for part in _CODE.findall(raw))


def fetch_one(item, throttle, retries=2):
    cve, syn, url, source, host = item
    for attempt in range(retries + 1):
        throttle.wait(host)
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                raw = r.read(1 << 20).decode("utf-8", "replace")
            break
        except urllib.error.HTTPError as e:
            if e.code in (429, 503) and attempt < retries:
                time.sleep(30)
                continue
            return {"cve_id": cve, "source": source, "url": url, "sha256": None,
                    "payload": None, "status": f"http_{e.code}"}
        except Exception as e:                                  # noqa: BLE001
            if attempt < retries:
                time.sleep(5)
                continue
            return {"cve_id": cve, "source": source, "url": url, "sha256": None,
                    "payload": None, "status": type(e).__name__}
    else:
        return {"cve_id": cve, "source": source, "url": url, "sha256": None,
                "payload": None, "status": "retries"}
    text = to_text(raw)
    if _BLOCKED.search(text[:2000]):
        return {"cve_id": cve, "source": source, "url": url, "sha256": None,
                "payload": None, "status": "blocked"}
    digest = hashlib.sha256(raw.encode("utf-8", "replace")).hexdigest()
    payload = extract_payload(text, syn, prefer_constructed=True)
    return {"cve_id": cve, "source": source, "url": url, "sha256": digest,
            "payload": payload, "status": "payload" if payload else "no_payload"}


def run_ghsl(con, args):
    """Crawl pinned Security Lab advisories from the published sitemap."""
    req = urllib.request.Request(GHSL_SITEMAP, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as response:
        sitemap = response.read(2 << 20).decode("utf-8", "replace")
    done = {url for (url,) in con.execute(
        "SELECT url FROM ghsl_page WHERE sha256 IS NOT NULL")}
    urls = sorted(set(GHSL_URL.findall(sitemap)) - done)
    if args.n:
        urls = urls[:args.n]
    # Independent of derived groups: --refresh-ghsl deletes its own payload rows
    # before the groups are rebuilt, so using the old groups would lose results.
    syn_of = dict(con.execute("SELECT cve_id, syntax_type FROM classification"))
    throttle = Throttle(args.delay)
    print(f"{len(done)} GHSL pages already fetched; {len(urls)} to go",
          file=sys.stderr, flush=True)

    def fetch_page(url):
        throttle.wait("securitylab")
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=TIMEOUT) as response:
                raw = response.read(2 << 20).decode("utf-8", "replace")
        except urllib.error.HTTPError as error:
            return (url, None, f"http_{error.code}"), []
        except Exception as error:                              # noqa: BLE001
            return (url, None, type(error).__name__), []
        digest = hashlib.sha256(raw.encode("utf-8", "replace")).hexdigest()
        text = to_text(raw)
        ids = sorted(set(CVE_RE.findall(text)) & syn_of.keys())
        if not ids:
            return (url, digest, "no_injection_cve"), []
        if len(ids) > 1:
            return (url, digest, "ambiguous_cves"), []
        cve = ids[0]
        payload = (extract_payload(code_text(raw), syn_of[cve], prefer_constructed=True)
                   or extract_payload(text, syn_of[cve], prefer_constructed=True))
        status = "payload" if payload else "no_payload"
        row = {"cve_id": cve, "source": "ghsl", "url": url,
               "sha256": digest, "payload": payload, "status": status}
        return (url, digest, status), [row] if payload else []

    pages, hits = [], []
    with ThreadPoolExecutor(max_workers=8) as executor:
        for seen, future in enumerate(as_completed(
                [executor.submit(fetch_page, url) for url in urls]), 1):
            page, rows = future.result()
            pages.append(page)
            hits.extend(rows)
            if len(pages) >= 25 or seen == len(urls):
                con.executemany("INSERT OR REPLACE INTO ghsl_page VALUES(?,?,?)", pages)
                con.executemany("""INSERT OR REPLACE INTO web_payload
                    VALUES(:cve_id,:source,:url,:sha256,:payload,:status)""", hits)
                con.commit()
                pages.clear()
                hits.clear()
            if seen % 50 == 0 or seen == len(urls):
                got = con.execute("SELECT COUNT(*) FROM web_payload "
                                  "WHERE source='ghsl' AND payload IS NOT NULL").fetchone()[0]
                print(f"  {seen}/{len(urls)}  payloads={got}", file=sys.stderr, flush=True)


def run(args):
    con = sqlite3.connect(DB)
    con.executescript(DDL)
    if args.refresh_ghsl:
        con.execute("DELETE FROM web_payload WHERE source='ghsl'")
        con.execute("DELETE FROM ghsl_page")
        con.commit()
    if args.ghsl or args.refresh_ghsl:
        run_ghsl(con, args)
        finish(con)
        return
    done = {c for (c,) in con.execute("SELECT cve_id FROM web_payload")}
    todo = candidates(con, done)
    if args.n:
        todo = todo[:args.n]
    by_host = {}
    for t in todo:
        by_host[t[4]] = by_host.get(t[4], 0) + 1
    print(f"{len(done)} already fetched; {len(todo)} to go {by_host}",
          file=sys.stderr, flush=True)
    if not todo:
        finish(con)
        return

    throttle = Throttle(args.delay)
    batch, t0 = [], time.monotonic()
    # Partitioned by host, one thread per partition, results streamed back as
    # they finish. Mapping over the combined list instead would let both threads
    # draw consecutive same-host items and queue behind the same lock, wasting
    # the other host's idle capacity; collecting per-lane lists instead would
    # write nothing until the slowest host finished, losing every checkpoint.
    lanes = {}
    for t in todo:
        lanes.setdefault(t[4], []).append(t)
    out_q = queue.Queue()

    def drain(items):
        strikes = 0
        for it in items:
            if strikes >= BLOCK_LIMIT:
                out_q.put({"cve_id": it[0], "source": it[3], "url": it[2],
                           "sha256": None, "payload": None, "status": "skipped_blocked"})
                continue
            r = fetch_one(it, throttle)
            strikes = strikes + 1 if r["status"] == "blocked" else 0
            if strikes == BLOCK_LIMIT:
                print(f"  {it[3]}: blocked {BLOCK_LIMIT} times, abandoning this site",
                      file=sys.stderr, flush=True)
            out_q.put(r)

    threads = [threading.Thread(target=drain, args=(items,), daemon=True)
               for items in lanes.values()]
    for t in threads:
        t.start()

    for i in range(1, len(todo) + 1):
        batch.append(out_q.get())
        if i % 50 == 0 or i == len(todo):
            con.executemany("""INSERT OR REPLACE INTO web_payload
                VALUES(:cve_id, :source, :url, :sha256, :payload, :status)""", batch)
            con.commit()
            batch.clear()
            got = con.execute("SELECT COUNT(*) FROM web_payload "
                              "WHERE payload IS NOT NULL").fetchone()[0]
            print(f"  {i}/{len(todo)}  payloads={got}  "
                  f"{i / max(time.monotonic() - t0, 1):.1f}/s",
                  file=sys.stderr, flush=True)
    for t in threads:
        t.join(timeout=5)

    for s, n in con.execute("SELECT status, COUNT(*) FROM web_payload GROUP BY 1 ORDER BY 2 DESC"):
        print(f"  {s:12} {n}")
    finish(con)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-n", type=int, help="fetch only this many, for measurement")
    ap.add_argument("--delay", type=float, default=1.0,
                    help="minimum seconds between requests to the same host")
    ap.add_argument("--ghsl", action="store_true",
                    help="crawl GitHub Security Lab advisories from its sitemap")
    ap.add_argument("--refresh-ghsl", action="store_true",
                    help="replace the derived GHSL cache and payloads")
    run(ap.parse_args())
