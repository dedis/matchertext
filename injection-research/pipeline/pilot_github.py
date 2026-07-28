"""Pilot: measure what fetching linked GitHub PoC repos would actually yield.

30,608 injection CVEs carry a GitHub PoC link and no local file, but most of
those links point at CVE mirrors rather than exploits; excluding them (see
candidates) leaves roughly 12,600 addressable CVEs. Before committing to that
fetch we need three numbers this script measures on a random sample:

  reach   fraction of repos still resolvable (PoC repos are deleted and renamed
          constantly, so link rot is itself a result worth reporting)
  yield   fraction of reachable CVEs from which extract_payload recovers a
          payload, to compare against the 63% achieved on local files
  skew    whether that yield differs between matcher-hostable syntaxes and the
          rest, since an uneven yield biases the containment share

Fetches the tarball rather than cloning: one request per repo, no history, and
only text files under a size cap are read. Nothing is retained -- the archive is
parsed in memory and dropped, so no exploit code is written to disk.
"""
import argparse
import io
import json
import random
import re
import sqlite3
import sys
import tarfile
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import matchertext
from report import extract_payload

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
OUT = ROOT / "data" / "exports" / "pilot_github.json"

CODELOAD = "https://codeload.github.com/{owner}/{repo}/tar.gz/{ref}"
REPO_RE = re.compile(r"https?://(?:www\.)?github\.com/([\w.-]+)/([\w.-]+)", re.I)
# Where literal payloads live. Exploit scripts that build payloads at runtime
# are the known dead end, so prose and data files come first.
TEXT_EXT = {".md", ".txt", ".rst", ".json", ".yaml", ".yml", ".html", ".xml",
            ".py", ".php", ".rb", ".sh", ".js", ".go", ".java", ".pl", ""}
MAX_MEMBER = 256 << 10      # skip anything too big to be a PoC note or script
MAX_ARCHIVE = 25 << 20     # a PoC repo larger than this is not a PoC repo
TIMEOUT = 25


def repo_of(url):
    m = REPO_RE.match(url.strip())
    if not m:
        return None
    return m.group(1), m.group(2).removesuffix(".git")


def candidates(con, n, seed, max_fanout=20):
    """Sample CVEs that have a GitHub link, no local file, and no payload yet.

    A repo linked from thousands of CVEs is a mirror, not a proof of concept:
    trickest's Github section lists any repo mentioning the CVE, which sweeps in
    bulk trackers (nuclei-template forks, nvd-json-data-feeds, cvemon). They are
    large and contain no payload, so repos above max_fanout are dropped and each
    CVE is represented by its most specific repo. CVEs whose only links are
    aggregators are not addressable at all and are excluded.
    """
    rows = con.execute("""
        SELECT cl.cve_id, cl.syntax_type, p.ref
        FROM classification cl JOIN poc p USING(cve_id)
        WHERE p.local_path IS NULL AND p.ref LIKE '%github.com%'
          AND cl.cve_id NOT IN (SELECT cve_id FROM poc WHERE local_path IS NOT NULL)
          AND cl.cve_id NOT IN (SELECT cve_id FROM syntactic_group)
        ORDER BY cl.cve_id, p.ref""").fetchall()
    pairs, fan = set(), {}
    for cve, syn, ref in rows:
        r = repo_of(ref)
        if r:
            pairs.add((cve, syn, r))
            fan[r] = fan.get(r, 0) + 1
    by_cve = {}
    for cve, syn, r in sorted(pairs):
        if fan[r] > max_fanout:
            continue
        if cve not in by_cve or fan[r] < fan[by_cve[cve][1]]:
            by_cve[cve] = (syn, r)
    keys = sorted(by_cve)
    random.Random(seed).shuffle(keys)
    print(f"addressable CVEs (repo fan-out <= {max_fanout}): {len(keys)}", file=sys.stderr)
    return [(k, *by_cve[k]) for k in keys[:n]]


def fetch(owner, repo):
    """Return the whole archive, or a short failure reason.

    Never returns a truncated archive: a cut-off gzip stream cannot be listed,
    so an oversized repo is skipped rather than half-read.
    """
    for ref in ("HEAD", "master", "main"):
        url = CODELOAD.format(owner=owner, repo=repo, ref=ref)
        req = urllib.request.Request(url, headers={"User-Agent": "matchertext-research"})
        try:
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                declared = int(r.headers.get("Content-Length") or 0)
                if declared > MAX_ARCHIVE:
                    return None, "oversize"
                blob = r.read(MAX_ARCHIVE + 1)
                if len(blob) > MAX_ARCHIVE:
                    return None, "oversize"
                return blob, None
        except urllib.error.HTTPError as e:
            if e.code in (404, 451) and ref != "main":
                continue
            return None, f"http_{e.code}"
        except Exception as e:                                  # noqa: BLE001
            return None, type(e).__name__
    return None, "http_404"


def scan(blob, syn):
    """Run extract_payload over the archive's text members."""
    try:
        tf = tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz")
        members = [m for m in tf.getmembers()
                   if m.isfile() and m.size <= MAX_MEMBER
                   and Path(m.name).suffix.lower() in TEXT_EXT]
    except Exception:                                           # noqa: BLE001
        return None, "bad_archive"
    # READMEs first: vulhub showed prose carries the worked payload far more
    # often than the exploit script does.
    members.sort(key=lambda m: (0 if "readme" in Path(m.name).name.lower() else 1,
                                m.name))
    for m in members:
        try:
            text = tf.extractfile(m).read().decode("utf-8", "replace")
        except Exception:                                       # noqa: BLE001
            continue
        payload = extract_payload(text, syn)
        if payload:
            return payload, m.name
    return None, None


def one(item):
    cve, syn, (owner, repo) = item
    blob, err = fetch(owner, repo)
    if blob is None:
        return {"cve": cve, "syntax": syn, "repo": f"{owner}/{repo}",
                "ok": False, "error": err}
    payload, where = scan(blob, syn)
    return {"cve": cve, "syntax": syn, "repo": f"{owner}/{repo}", "ok": True,
            "bytes": len(blob), "payload": payload, "file": where}


def run(args):
    con = sqlite3.connect(DB)
    sample = candidates(con, args.n, args.seed, args.max_fanout)
    print(f"sampling {len(sample)} CVEs with a GitHub PoC link and no payload",
          file=sys.stderr)
    results = []
    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        for i, r in enumerate(ex.map(one, sample), 1):
            results.append(r)
            if i % 50 == 0:
                got = sum(bool(x.get("payload")) for x in results)
                reach = sum(x["ok"] for x in results)
                print(f"  {i}/{len(sample)}  reachable={reach}  payloads={got}",
                      file=sys.stderr, flush=True)
    OUT.write_text(json.dumps(results, indent=1))

    n = len(results)
    reachable = [r for r in results if r["ok"]]
    got = [r for r in reachable if r.get("payload")]
    print(f"\nsampled            {n}")
    print(f"reachable repos    {len(reachable)} ({len(reachable) / n:.0%})")
    print(f"payload recovered  {len(got)} "
          f"({len(got) / n:.0%} of sample, {len(got) / max(len(reachable), 1):.0%} of reachable)")
    errs = {}
    for r in results:
        if not r["ok"]:
            errs[r["error"]] = errs.get(r["error"], 0) + 1
    print("failures:", dict(sorted(errs.items(), key=lambda kv: -kv[1])))
    print("\nyield by syntax class:")
    for label, keep in (("matcher-hostable", True), ("outside", False)):
        sub = [r for r in reachable if (r["syntax"] in matchertext.HOST) == keep]
        hit = sum(bool(r.get("payload")) for r in sub)
        if sub:
            print(f"  {label:17} {hit}/{len(sub)} ({hit / len(sub):.0%})")
    print("\nwhere the payload came from:")
    src = {}
    for r in got:
        k = "README" if "readme" in (r["file"] or "").lower() else Path(r["file"]).suffix or "other"
        src[k] = src.get(k, 0) + 1
    print(" ", dict(sorted(src.items(), key=lambda kv: -kv[1])))
    print(f"\nfull results: {OUT.relative_to(ROOT)}")
    con.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-n", type=int, default=500)
    ap.add_argument("--seed", type=int, default=17)
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--max-fanout", type=int, default=20,
                    help="drop repos linked from more CVEs than this (aggregators)")
    run(ap.parse_args())
