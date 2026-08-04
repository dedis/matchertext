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
  python3 pipeline/fetch_github.py --retry --archives  # recursively retry misses
  python3 pipeline/fetch_github.py --trees    # scan oversized repos file by file
  python3 pipeline/fetch_github.py --fanout-trees  # exact CVE paths in collections
"""
import argparse
import io
import json
import queue
import random
import re
import sqlite3
import subprocess
import sys
import tarfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from report import extract_payload

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
PINS = ROOT / "data" / "github_pins.json"

CODELOAD = "https://codeload.github.com/{owner}/{repo}/tar.gz/{ref}"
RAW_URL = "https://raw.githubusercontent.com/{owner}/{repo}/{sha}/{path}"
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
MAX_SCAN = 8 << 20
MAX_FILES = 2000
MAX_TREE_FILES = 64
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
    """Resolve HEAD to a commit SHA over the git protocol, not the REST API.

    Fallback for when the batched GraphQL path is unavailable.
    """
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


def _gql(query):
    """Run one GraphQL query. gh exits non-zero when any alias 404s, but the
    surviving aliases are still in `data`, so parse regardless of exit code."""
    try:
        out = subprocess.run(["gh", "api", "graphql", "-f", "query=" + query],
                             capture_output=True, text=True, timeout=180)
        return (json.loads(out.stdout) or {}).get("data") or {}
    except Exception:                                           # noqa: BLE001
        return {}


def resolve_batch(repos):
    """SHA, root file listing and README text for many repos in one request.

    Downloading whole archives turned out to be bandwidth-bound: 24 concurrent
    codeload fetches saturated the link at ~283 KB/s, which no amount of
    threading or cores can fix. The tree gives the filenames for a few KB, so
    only the handful of documents worth reading are ever transferred.

    Returns {(owner, name): (sha_or_None, [entry names], readme_text_or_None)}.
    A deleted repo yields (None, [], None) without failing the batch.
    """
    q = ["query {"]
    for i, (o, n) in enumerate(repos):
        q.append(f"  r{i}: repository(owner:{json.dumps(o)}, name:{json.dumps(n)}) {{"
                 f" defaultBranchRef {{ target {{ ... on Commit {{ oid }} }} }}"
                 f" tree: object(expression:\"HEAD:\") {{ ... on Tree {{ entries {{ name type }} }} }}"
                 f" readme: object(expression:\"HEAD:README.md\") {{ ... on Blob {{ text }} }} }}")
    q.append("}")
    data = _gql("\n".join(q))
    result = {}
    for i, key in enumerate(repos):
        node = data.get(f"r{i}")
        if not node:
            result[key] = (None, [], None)
            continue
        target = (node.get("defaultBranchRef") or {}).get("target") or {}
        tree = node.get("tree") or {}
        entries = [(e.get("name"), e.get("type")) for e in (tree.get("entries") or ())]
        result[key] = (target.get("oid"), entries,
                       (node.get("readme") or {}).get("text"))
    return result


def fetch_blobs(items):
    """Fetch specific file contents in one request.

    items: [((owner, name), path), ...]. Returns {(key, path): text_or_None}.
    """
    if not items:
        return {}
    q = ["query {"]
    for i, ((o, n), path) in enumerate(items):
        q.append(f"  b{i}: repository(owner:{json.dumps(o)}, name:{json.dumps(n)}) {{"
                 f" object(expression:{json.dumps('HEAD:' + path)})"
                 f" {{ ... on Blob {{ text }} }} }}")
    q.append("}")
    data = _gql("\n".join(q))
    out = {}
    for i, key_path in enumerate(items):
        node = data.get(f"b{i}") or {}
        out[key_path] = ((node.get("object") or {}).get("text"))
    return out


def subdir_paths(repos_dirs):
    """List one level down, for repos whose root documents yielded nothing.

    items: [((owner, name), dirname), ...] -> {(key, dirname): [file names]}
    """
    if not repos_dirs:
        return {}
    q = ["query {"]
    for i, ((o, n), d) in enumerate(repos_dirs):
        q.append(f"  d{i}: repository(owner:{json.dumps(o)}, name:{json.dumps(n)}) {{"
                 f" object(expression:{json.dumps('HEAD:' + d)})"
                 f" {{ ... on Tree {{ entries {{ name type }} }} }} }}")
    q.append("}")
    data = _gql("\n".join(q))
    out = {}
    for i, key_dir in enumerate(repos_dirs):
        node = (data.get(f"d{i}") or {}).get("object") or {}
        out[key_dir] = [e.get("name") for e in (node.get("entries") or ())
                        if e.get("type") == "blob"]
    return out


# Directories worth descending into when the root turned up nothing. Anchored on
# name segments: an unanchored "doc" matches docker and an unanchored "report"
# matches aj-report, neither of which holds a write-up.
DIR_HINT = re.compile(
    r"(?:^|[-_.])(?:pocs?|exploits?|payloads?|vulns?|attacks?|docs?|"
    r"writeups?|reports?|cve)(?:$|[-_.])", re.I)


def doc_paths(entries, limit):
    """Root-level files worth reading, READMEs first.

    Dotfiles are skipped: they are extensionless, so they satisfy DOC_EXT and
    would otherwise spend the per-repo budget on .gitattributes rather than the
    write-up.
    """
    names = [n for n, t in entries
             if t == "blob" and n and not n.startswith(".") and eligible(n)]
    names.sort(key=lambda n: (0 if "readme" in n.lower() else 1, n))
    return names[:limit]


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
    scanned = 0
    for m in members[:MAX_FILES]:
        if scanned + m.size > MAX_SCAN:
            continue
        scanned += m.size
        try:
            text = tf.extractfile(m).read().decode("utf-8", "replace")
        except Exception:                                       # noqa: BLE001
            continue
        payload = extract_payload(text, syn)
        if payload:
            return payload, m.name
    return None, None


def recursive_tree(owner, repo, sha):
    """List all files at one pinned commit through GitHub's tree API."""
    try:
        out = subprocess.run(
            ["gh", "api", f"repos/{owner}/{repo}/git/trees/{sha}?recursive=1"],
            capture_output=True, text=True, timeout=TIMEOUT)
        if out.returncode:
            return None, False, "tree_api_error"
        data = json.loads(out.stdout)
        return data.get("tree") or [], bool(data.get("truncated")), None
    except (subprocess.TimeoutExpired, json.JSONDecodeError):
        return None, False, "tree_api_error"


def fetch_text(owner, repo, sha, path):
    url = RAW_URL.format(owner=owner, repo=repo, sha=sha,
                         path=urllib.parse.quote(path, safe="/"))
    req = urllib.request.Request(url, headers={"User-Agent": "matchertext-research"})
    try:
        with urllib.request.urlopen(req, timeout=12) as r:
            blob = r.read(MAX_MEMBER + 1)
        return None if len(blob) > MAX_MEMBER else blob.decode("utf-8", "replace")
    except Exception:                                           # noqa: BLE001
        return None


def scan_tree(item, sha):
    cve, syn, (owner, repo) = item
    entries, truncated, err = recursive_tree(owner, repo, sha)
    if entries is None:
        return {"cve_id": cve, "repo": f"{owner}/{repo}", "sha": sha,
                "file": None, "payload": None, "status": err}
    files = [e for e in entries if e.get("type") == "blob"
             and (e.get("size") or 0) <= MAX_MEMBER and eligible(e.get("path", ""))]
    files.sort(key=lambda e: (
        0 if POC_HINT.search(e["path"]) else 1,
        0 if "readme" in Path(e["path"]).name.lower() else 1,
        0 if Path(e["path"]).suffix.lower() in DOC_EXT else 1, e["path"]))
    scanned = 0
    for e in files[:MAX_TREE_FILES]:
        size = e.get("size") or 0
        if scanned + size > MAX_SCAN:
            continue
        scanned += size
        text = fetch_text(owner, repo, sha, e["path"])
        payload = extract_payload(text, syn) if text else None
        if payload:
            return {"cve_id": cve, "repo": f"{owner}/{repo}", "sha": sha,
                    "file": e["path"], "payload": payload, "status": "payload"}
    status = "tree_truncated" if truncated else "tree_no_payload"
    return {"cve_id": cve, "repo": f"{owner}/{repo}", "sha": sha,
            "file": None, "payload": None, "status": status}


def scan_fanout_group(group):
    """Scan one multi-CVE repository without crossing CVE file boundaries."""
    (owner, repo), items = group
    sha = items[0][2]
    entries, truncated, err = recursive_tree(owner, repo, sha)
    if entries is None:
        return [{"cve_id": cve, "repo": f"{owner}/{repo}", "sha": sha,
                 "file": None, "payload": None, "status": "fanout_api_error"}
                for cve, _, _ in items]
    blobs = [e for e in entries if e.get("type") == "blob"
             and (e.get("size") or 0) <= MAX_MEMBER and eligible(e.get("path", ""))]
    rows, cache = [], {}
    for cve, syn, _ in items:
        needle = cve.lower()
        files = [e for e in blobs
                 if needle in e["path"].replace("_", "-").lower()]
        files.sort(key=lambda e: (
            0 if "readme" in Path(e["path"]).name.lower() else 1,
            0 if Path(e["path"]).suffix.lower() in DOC_EXT else 1, e["path"]))
        hit = where = None
        scanned = 0
        for e in files[:16]:
            size = e.get("size") or 0
            if scanned + size > (2 << 20):
                continue
            scanned += size
            path = e["path"]
            if path not in cache:
                cache[path] = fetch_text(owner, repo, sha, path)
            text = cache[path]
            hit = extract_payload(text, syn) if text else None
            if hit:
                where = path
                break
        status = ("payload" if hit else "fanout_no_payload" if files
                  else "fanout_no_path")
        if truncated and not files:
            status = "fanout_truncated"
        rows.append({"cve_id": cve, "repo": f"{owner}/{repo}", "sha": sha,
                     "file": where, "payload": hit, "status": status})
    return rows


def run_fanout(con, args):
    groups = {}
    for cve, syn, repo, sha in con.execute("""
            SELECT r.cve_id, c.syntax_type, r.repo, r.sha
            FROM remote_payload r JOIN classification c USING(cve_id)
            WHERE r.status IN ('no_payload','fanout_api_error') AND r.sha IS NOT NULL
            ORDER BY r.repo, r.cve_id"""):
        parts = repo.split("/", 1)
        if len(parts) == 2:
            groups.setdefault(tuple(parts), []).append((cve, syn, sha))
    work = sorted(groups.items())
    if args.n:
        random.Random(args.seed).shuffle(work)
        work = work[:args.n]
    total = sum(len(items) for _, items in work)
    print(f"{len(work)} repositories; {total} CVEs to scan", file=sys.stderr, flush=True)
    seen = 0
    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        futures = [ex.submit(scan_fanout_group, group) for group in work]
        for future in as_completed(futures):
            rows = future.result()
            con.executemany("""INSERT OR REPLACE INTO remote_payload
                VALUES(:cve_id, 'github', :repo, :sha, :file, :payload, :status)""", rows)
            con.commit()
            seen += len(rows)
            got = con.execute("SELECT COUNT(*) FROM remote_payload "
                              "WHERE payload IS NOT NULL").fetchone()[0]
            print(f"  {seen}/{total}  payloads={got}", file=sys.stderr, flush=True)


def one(item, sha=None):
    cve, syn, (owner, repo) = item
    slug = f"{owner}/{repo}"
    if sha is None:
        sha, err = resolve_sha(owner, repo)
    if sha is None:
        return {"cve_id": cve, "repo": slug, "sha": None,
                "file": None, "payload": None, "status": "gone"}
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
    if args.fanout_trees:
        run_fanout(con, args)
        con.close()
        return
    tree_shas = {}
    if args.trees:
        todo = []
        for cve, syn, repo, sha in con.execute("""
                SELECT r.cve_id, c.syntax_type, r.repo, r.sha
                FROM remote_payload r JOIN classification c USING(cve_id)
                WHERE r.status='oversize' AND r.sha IS NOT NULL
                ORDER BY r.cve_id"""):
            parts = repo.split("/", 1)
            if len(parts) == 2:
                todo.append((cve, syn, tuple(parts)))
                tree_shas[cve] = sha
        done = set()
    elif args.archives:
        done_sql = ("SELECT cve_id FROM remote_payload WHERE payload IS NOT NULL "
                    "OR status IN ('gone','deep_no_payload','oversize','http_404')")
    elif args.retry:
        done_sql = ("SELECT cve_id FROM remote_payload WHERE payload IS NOT NULL "
                    "OR status='gone'")
    else:
        done_sql = "SELECT cve_id FROM remote_payload"
    if not args.trees:
        done = {c for (c,) in con.execute(done_sql)}
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

    if args.trees:
        seen = 0
        with ThreadPoolExecutor(max_workers=args.workers) as ex:
            futures = [ex.submit(scan_tree, item, tree_shas[item[0]]) for item in todo]
            for future in as_completed(futures):
                row = future.result()
                batch.append(row)
                seen += 1
                if len(batch) >= 25:
                    flush()
                if seen % 25 == 0 or seen == len(todo):
                    got = con.execute("SELECT COUNT(*) FROM remote_payload "
                                      "WHERE payload IS NOT NULL").fetchone()[0]
                    print(f"  {seen}/{len(todo)}  payloads={got}  "
                          f"{seen / max(time.time() - t0, 1):.1f}/s",
                          file=sys.stderr, flush=True)
        if batch:
            flush()
        total = con.execute("SELECT COUNT(*) FROM remote_payload").fetchone()[0]
        print(f"\nfetched {total} repos; pins in {PINS.relative_to(ROOT)}")
        for st, n in con.execute("""SELECT status, COUNT(*) FROM remote_payload
                                    GROUP BY 1 ORDER BY 2 DESC"""):
            print(f"  {st:20} {n}")
        con.close()
        return

    if args.archives:
        stored = dict(con.execute(
            "SELECT repo, sha FROM remote_payload WHERE sha IS NOT NULL"))

        def archive_one(item):
            owner, repo = item[2]
            slug = f"{owner}/{repo}"
            row = one(item, pins.get(slug) or stored.get(slug))
            if row["status"] == "no_payload":
                row["status"] = "deep_no_payload"
            return row

        seen = 0
        with ThreadPoolExecutor(max_workers=args.workers) as ex:
            for row in ex.map(archive_one, todo):
                batch.append(row)
                if row["sha"]:
                    pins[row["repo"]] = row["sha"]
                seen += 1
                if len(batch) >= 50:
                    flush()
                if seen % 50 == 0 or seen == len(todo):
                    got = con.execute("SELECT COUNT(*) FROM remote_payload "
                                      "WHERE payload IS NOT NULL").fetchone()[0]
                    print(f"  {seen}/{len(todo)}  payloads={got}  "
                          f"{seen / max(time.time() - t0, 1):.1f}/s",
                          file=sys.stderr, flush=True)
        if batch:
            flush()
        total = con.execute("SELECT COUNT(*) FROM remote_payload").fetchone()[0]
        print(f"\nfetched {total} repos; pins in {PINS.relative_to(ROOT)}")
        for st, n in con.execute("""SELECT status, COUNT(*) FROM remote_payload
                                    GROUP BY 1 ORDER BY 2 DESC"""):
            print(f"  {st:12} {n}")
        con.close()
        return

    def do_chunk(chunk):
        """Two requests per chunk: listing, then the documents worth reading."""
        resolved = resolve_batch([r for _, _, r in chunk])
        rows, wanted = [], []
        for cve, syn, key in chunk:
            sha, entries, readme = resolved.get(key, (None, [], None))
            if sha is None:
                rows.append({"cve_id": cve, "repo": "/".join(key), "sha": None,
                             "file": None, "payload": None, "status": "gone"})
                continue
            payload = extract_payload(readme, syn) if readme else None
            if payload:
                rows.append({"cve_id": cve, "repo": "/".join(key), "sha": sha,
                             "file": "README.md", "payload": payload,
                             "status": "payload"})
                continue
            paths = [p for p in doc_paths(entries, args.docs) if p != "README.md"]
            wanted.append((cve, syn, key, sha, paths, entries))
        blobs = fetch_blobs([(key, p) for _, _, key, _, ps, _ in wanted for p in ps])
        deep, resolved_rows = [], []
        for cve, syn, key, sha, paths, entries in wanted:
            hit = hitfile = None
            for p in paths:
                text = blobs.get((key, p))
                hit = extract_payload(text, syn) if text else None
                if hit:
                    hitfile = p
                    break
            if hit or not args.deep:
                resolved_rows.append({"cve_id": cve, "repo": "/".join(key), "sha": sha,
                                      "file": hitfile, "payload": hit,
                                      "status": "payload" if hit else "no_payload"})
            else:
                dirs = [n for n, t in entries
                        if t == "tree" and n and not n.startswith(".") and DIR_HINT.search(n)]
                deep.append((cve, syn, key, sha, dirs[:args.dirs]))
        rows.extend(resolved_rows)

        # Second pass: the write-up is sometimes filed under docs/ or poc/
        # rather than at the root.
        listing = subdir_paths([(key, d) for _, _, key, _, ds in deep for d in ds])
        deep_want = []
        for cve, syn, key, sha, dirs in deep:
            paths = []
            for d in dirs:
                for name in listing.get((key, d), ())[:args.docs]:
                    if eligible(name) and not name.startswith("."):
                        paths.append(f"{d}/{name}")
            deep_want.append((cve, syn, key, sha, paths[:args.docs]))
        blobs2 = fetch_blobs([(key, p) for _, _, key, _, ps in deep_want for p in ps])
        for cve, syn, key, sha, paths in deep_want:
            hit = hitfile = None
            for p in paths:
                text = blobs2.get((key, p))
                hit = extract_payload(text, syn) if text else None
                if hit:
                    hitfile = p
                    break
            rows.append({"cve_id": cve, "repo": "/".join(key), "sha": sha,
                         "file": hitfile, "payload": hit,
                         "status": "payload" if hit else "no_payload"})
        return rows

    # Whole-archive downloads were bandwidth-bound; reading only the listed
    # documents removes the transfer from the critical path, so chunks can just
    # run concurrently with no producer/consumer machinery.
    chunks = [todo[i:i + args.gql_batch] for i in range(0, len(todo), args.gql_batch)]
    seen = 0
    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        for rows in ex.map(do_chunk, chunks):
            for r in rows:
                batch.append(r)
                if r["sha"]:
                    pins[r["repo"]] = r["sha"]
            seen += len(rows)
            flush()
            got = con.execute("SELECT COUNT(*) FROM remote_payload "
                              "WHERE payload IS NOT NULL").fetchone()[0]
            print(f"  {seen}/{len(todo)}  payloads={got}  "
                  f"{seen / max(time.time() - t0, 1):.1f}/s", file=sys.stderr, flush=True)
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
    ap.add_argument("--retry", action="store_true",
                    help="re-process repos previously found to have no payload")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--archives", action="store_true",
                      help="recursively scan pinned repository archives")
    mode.add_argument("--trees", action="store_true",
                      help="scan oversized repositories through pinned Git trees")
    mode.add_argument("--fanout-trees", action="store_true",
                      help="scan exact CVE paths in multi-CVE repositories")
    ap.add_argument("--workers", type=int, default=8,
                    help="concurrent archive downloads; the work is I/O bound,\nso this is the lever, not core count")
    ap.add_argument("--deep", action="store_true",
                    help="descend one level into doc/poc directories")
    ap.add_argument("--dirs", type=int, default=3,
                    help="subdirectories to descend into per repo")
    ap.add_argument("--docs", type=int, default=4,
                    help="root-level documents to read per repo")
    ap.add_argument("--gql-batch", type=int, default=25,
                    help="repos per batched GraphQL request")
    ap.add_argument("--no-gql", dest="gql", action="store_false",
                    help="skip GraphQL batching, resolve each SHA with git ls-remote")
    ap.add_argument("--max-fanout", type=int, default=20,
                    help="drop repos linked from more CVEs than this (aggregators)")
    run(ap.parse_args())
