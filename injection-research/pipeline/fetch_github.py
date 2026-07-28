"""Stage 1b: recover payloads from linked GitHub PoC repositories.

Most injection CVEs with a PoC link have no local file, so no payload can be
extracted from them. This resolves each CVE's most specific linked repo to a
commit SHA, fetches that exact commit, and keeps only the extracted payload.

Reproducibility is the reason for the SHA. A quarter of these repos are already
gone and HEAD moves under the rest, so a corpus fetched by branch name cannot be
replayed. `git ls-remote` resolves the SHA without spending API quota, and
codeload serves that commit directly -- the archive's root directory is
`repo-<sha>`, so the pin is self-evidencing. Pins land in data/github_pins.json.

Only the payload is stored, never the repo: archives are parsed in memory and
dropped, so no exploit code is written to disk (see the dual-use note in
doc/data.tex).

Aggregators are excluded. trickest's Github section lists any repo mentioning a
CVE, which sweeps in bulk mirrors -- one nuclei-templates fork is linked from
11,758 CVEs -- and they are large, payload-free, and would dominate the fetch.

Usage:
  python3 pipeline/fetch_github.py            # full run, resumable
  python3 pipeline/fetch_github.py -n 500     # sample, for measurement
"""
import argparse
import io
import json
import random
import re
import sqlite3
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from report import extract_payload

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
PINS = ROOT / "data" / "github_pins.json"

CODELOAD = "https://codeload.github.com/{owner}/{repo}/tar.gz/{ref}"
REPO_RE = re.compile(r"https?://(?:www\.)?github\.com/([\w.-]+)/([\w.-]+)", re.I)
# A linked repo is often the vulnerable application itself, not a proof of
# concept, and its own source is full of benign script tags, jQuery includes and
# CLI flags that read as payloads. Documentation is always searched, because that
# is where a PoC is written up; code is searched only when its path says the file
# is the exploit. Everything else is skipped, which costs a few genuine payloads
# and removes a much larger amount of application source.
DOC_EXT = {".md", ".txt", ".rst", ""}
CODE_EXT = {".py", ".php", ".rb", ".sh", ".js", ".go", ".java", ".pl", ".html",
            ".json", ".yaml", ".yml", ".xml"}
POC_HINT = re.compile(r"(?:poc|exploit|payload|vuln|attack|cve-\d)", re.I)
MAX_MEMBER = 256 << 10
MAX_ARCHIVE = 25 << 20
TIMEOUT = 25

DDL = """
CREATE TABLE IF NOT EXISTS remote_payload(
    cve_id TEXT PRIMARY KEY, source TEXT, repo TEXT, sha TEXT,
    file TEXT, payload TEXT, status TEXT);
"""


def repo_of(url):
    m = REPO_RE.match(url.strip())
    if not m:
        return None
    return m.group(1), m.group(2).removesuffix(".git")


def candidates(con, max_fanout, done):
    """CVEs with a specific (non-aggregator) GitHub repo and no payload yet."""
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
        if fan[r] > max_fanout or cve in done:
            continue
        if cve not in by_cve or fan[r] < fan[by_cve[cve][1]]:
            by_cve[cve] = (syn, r)
    return [(k, *by_cve[k]) for k in sorted(by_cve)]


def resolve_sha(owner, repo):
    """Resolve HEAD to a commit SHA over the git protocol, not the REST API."""
    try:
        out = subprocess.run(
            ["git", "ls-remote", f"https://github.com/{owner}/{repo}", "HEAD"],
            capture_output=True, text=True, timeout=TIMEOUT,
            env={"GIT_TERMINAL_PROMPT": "0", "PATH": "/usr/bin:/bin:/usr/local/bin"})
    except subprocess.TimeoutExpired:
        return None, "timeout"
    if out.returncode != 0 or not out.stdout.strip():
        return None, "gone"
    return out.stdout.split()[0], None


def fetch(owner, repo, sha):
    url = CODELOAD.format(owner=owner, repo=repo, ref=sha)
    req = urllib.request.Request(url, headers={"User-Agent": "matchertext-research"})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                if int(r.headers.get("Content-Length") or 0) > MAX_ARCHIVE:
                    return None, "oversize"
                blob = r.read(MAX_ARCHIVE + 1)
                # Never return a truncated archive: a cut-off gzip cannot be
                # listed, so an oversized repo is skipped rather than half-read.
                return (None, "oversize") if len(blob) > MAX_ARCHIVE else (blob, None)
        except urllib.error.HTTPError as e:
            if e.code in (429, 503) and attempt < 2:
                time.sleep(2 ** attempt * 5)
                continue
            return None, f"http_{e.code}"
        except Exception as e:                                  # noqa: BLE001
            if attempt < 2:
                time.sleep(2)
                continue
            return None, type(e).__name__
    return None, "retries"


def eligible(name):
    """Documentation always; code only when the path claims to be the exploit."""
    suffix = Path(name).suffix.lower()
    if suffix in DOC_EXT:
        return True
    return suffix in CODE_EXT and bool(POC_HINT.search(name))


def scan(blob, syn):
    try:
        tf = tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz")
        members = [m for m in tf.getmembers()
                   if m.isfile() and m.size <= MAX_MEMBER and eligible(m.name)]
    except Exception:                                           # noqa: BLE001
        return None, "bad_archive"
    # READMEs first: prose carries the worked payload far more often than the
    # exploit script, which usually builds it at runtime.
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
    slug = f"{owner}/{repo}"
    sha, err = resolve_sha(owner, repo)
    if sha is None:
        return {"cve_id": cve, "repo": slug, "sha": None,
                "file": None, "payload": None, "status": err}
    blob, err = fetch(owner, repo, sha)
    if blob is None:
        return {"cve_id": cve, "repo": slug, "sha": sha,
                "file": None, "payload": None, "status": err}
    payload, where = scan(blob, syn)
    return {"cve_id": cve, "repo": slug, "sha": sha,
            "file": where if payload else None, "payload": payload,
            "status": "payload" if payload else "no_payload"}


def run(args):
    con = sqlite3.connect(DB)
    con.executescript(DDL)
    done = {c for (c,) in con.execute("SELECT cve_id FROM remote_payload")}
    todo = candidates(con, args.max_fanout, done)
    if args.n:
        random.Random(args.seed).shuffle(todo)
        todo = sorted(todo[:args.n])
    print(f"{len(done)} already fetched; {len(todo)} to go", file=sys.stderr, flush=True)

    pins = json.loads(PINS.read_text()) if PINS.exists() else {}
    batch, t0 = [], time.time()

    def flush():
        con.executemany(
            """INSERT OR REPLACE INTO remote_payload
               VALUES(:cve_id, 'github', :repo, :sha, :file, :payload, :status)""",
            batch)
        con.commit()
        PINS.write_text(json.dumps(pins, indent=1, sort_keys=True))
        batch.clear()

    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        for i, r in enumerate(ex.map(one, todo), 1):
            batch.append(r)
            if r["sha"]:
                pins[r["repo"]] = r["sha"]
            if i % 200 == 0 or i == len(todo):
                flush()
                got = con.execute("SELECT COUNT(*) FROM remote_payload "
                                  "WHERE payload IS NOT NULL").fetchone()[0]
                print(f"  {i}/{len(todo)}  payloads={got}  "
                      f"{i / max(time.time() - t0, 1):.1f}/s", file=sys.stderr, flush=True)
    if batch:
        flush()

    total = con.execute("SELECT COUNT(*) FROM remote_payload").fetchone()[0]
    print(f"\nfetched {total} repos; pins in {PINS.relative_to(ROOT)}")
    for st, n in con.execute("""SELECT status, COUNT(*) FROM remote_payload
                                GROUP BY 1 ORDER BY 2 DESC"""):
        print(f"  {st:12} {n}")
    con.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-n", type=int, help="sample this many instead of the full set")
    ap.add_argument("--seed", type=int, default=17)
    ap.add_argument("--workers", type=int, default=12)
    ap.add_argument("--max-fanout", type=int, default=20,
                    help="drop repos linked from more CVEs than this (aggregators)")
    run(ap.parse_args())
