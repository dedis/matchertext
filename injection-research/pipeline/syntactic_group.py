"""Stage 3c: group injection CVEs by syntactic closeness of their attack.

Each CVE with an extractable PoC payload is reduced to a *syntactic skeleton*:
string/number/identifier values become placeholders (<q> <n> <id>), while the
structural grammar — matchers () [] {}, operators, comment markers, quotes, tag
positions, and a curated set of injection keywords/sinks — is preserved. CVEs
sharing an identical skeleton form one syntactic group. Grouping is exact, so
it is fully deterministic and interpretable; specific literals never split a
group, but a different breakout structure always does.

Only PoC-backed CVEs are grouped (syntactic closeness needs the real payload).
"""
import argparse
import hashlib
import re
import sqlite3
from collections import defaultdict
from pathlib import Path

import matchertext
from export import write
from fetch_github import repo_of
from report import EXTENDED, TRUNCATED, accept, extract_payload

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
RAW = ROOT / "data" / "raw"

# Structural tokens preserved verbatim (uppercased); everything else identifier
# -> <id>. Tag names are abstracted separately to <TAG>.
KEYWORDS = {
    # SQL grammar
    "union", "select", "insert", "update", "delete", "drop", "create", "from",
    "where", "order", "group", "by", "having", "and", "or", "not", "null", "like",
    "limit", "offset", "exec", "execute", "declare", "cast", "convert", "concat",
    "substring", "char", "sleep", "waitfor", "delay", "benchmark", "pg_sleep",
    "extractvalue", "updatexml", "load_file", "outfile", "dumpfile", "into",
    "information_schema", "xp_cmdshell", "version", "database",
    # XSS sinks / handlers / attrs
    "onerror", "onload", "onmouseover", "onfocus", "onclick", "src", "href",
    "alert", "prompt", "confirm", "eval", "javascript", "document", "cookie",
    "location", "window", "fromcharcode",
    # shell
    "cat", "id", "whoami", "uname", "ping", "curl", "wget", "bash", "nc",
    "netcat", "powershell", "cmd", "echo",
    # template / expression / RCE
    "jndi", "ldap", "rmi", "dns", "runtime", "processbuilder", "popen", "system",
    "config", "self", "class", "globals", "builtins", "import", "subclasses",
}

_TOKEN = re.compile(r"""
    <\s*/\s* | < | > | -- | \#
  | [()\[\]{}] | ['"`]
  | [;&|=*/+~^%!?,.:@$\\-]
  | \d+ | [A-Za-z_]\w*(?:-\w+)* | \s+
""", re.X)
# A hyphen joins word characters only, so the "--" of `name--` stays a comment.
_WORD = re.compile(r"[A-Za-z_]\w*(?:-\w+)*")


def skeleton(payload):
    toks, prev = [], None
    for m in _TOKEN.finditer(payload):
        t = m.group(0)
        if t.isspace():
            continue
        if t.startswith("<"):  # '<' or '</'
            mapped = "</" if "/" in t else "<"
        elif prev in ("<", "</") and _WORD.fullmatch(t):
            mapped = "TAG"
        elif t in "'\"`":
            mapped = "<q>"
        elif t.isdigit():
            mapped = "<n>"
        elif _WORD.fullmatch(t):
            mapped = t.upper() if t.lower() in KEYWORDS else "<id>"
        else:
            mapped = t
        toks.append(mapped)
        prev = mapped if t.startswith("<") else t
    # collapse runs of identical placeholders (e.g. <id> <id> -> <id>) that only
    # reflect value length, not structure
    out = []
    for tok in toks:
        if tok in ("<id>", "<n>") and out and out[-1] == tok:
            continue
        out.append(tok)
    return " ".join(out)


def group_skeleton(syntax, payload):
    """The skeleton, led by matchertext.SINK when an HTML/DOM payload runs in an
    execution context, so that one group never mixes the two."""
    sk = skeleton(payload)
    if syntax in matchertext.PAYLOAD_DEPENDENT and matchertext.html_sink(payload):
        return f"{matchertext.SINK} {sk}"
    return sk


def representative_payload(con):
    """Yield (cve_id, syntax, payload, source, origin) for each CVE with a payload.

    The sources are tried in order, and source names the one that gave the
    payload: a local PoC file, a linked GitHub repository, advisory text, the
    CVE description, or an advisory page. origin locates it in that source.
    """
    syn_of = dict(con.execute("SELECT cve_id, syntax_type FROM classification"))
    paths = defaultdict(list)
    # Ordered so payload selection is stable across ingestions.
    for cid, lp in con.execute("""SELECT cve_id, local_path FROM poc
                                  WHERE local_path IS NOT NULL
                                  ORDER BY cve_id, source, ref"""):
        if cid in syn_of:
            paths[cid].append(lp)
    # Payloads recovered from linked GitHub repos, which have no local file.
    # Local files win: they are pinned in the corpus snapshot, whereas a remote
    # payload depends on a repo that may since have vanished. A repository
    # counts only if the CVE links it as PoC evidence: the other links name the
    # vendor's repository, whose documentation and code are not attacks.
    def _table(name):
        return con.execute("""SELECT COUNT(*) FROM sqlite_master
                              WHERE type='table' AND name=?""", (name,)).fetchone()[0]
    linked = defaultdict(set)
    for cid, ref in con.execute(
            "SELECT cve_id, ref FROM poc_evidence WHERE ref LIKE '%github.com/%'"):
        if repo := repo_of(ref):
            linked[cid].add(f"{repo[0]}/{repo[1]}".lower())
    remote = {cid: (payload, f"{repo}@{sha}:{file}") for cid, repo, sha, file, payload in con.execute(
        "SELECT cve_id, repo, sha, file, payload FROM remote_payload WHERE payload IS NOT NULL")
        if repo.lower() in linked[cid] and accept(payload)} if _table("remote_payload") else {}
    evidence = defaultdict(list)
    if _table("evidence_text"):
        for cid, ref, text in con.execute(
                "SELECT cve_id, ref, text FROM evidence_text ORDER BY cve_id, source, ref"):
            evidence[cid].append((ref, text))
    descriptions = dict(con.execute(
        "SELECT cve_id, description FROM cve WHERE description IS NOT NULL"))
    # Advisory pages are the last resort: unlike a pinned commit they can be
    # edited in place, so they are only consulted when nothing else has one.
    web = {cid: (payload, url) for cid, url, payload in con.execute(
        "SELECT cve_id, url, payload FROM web_payload WHERE payload IS NOT NULL")
        } if _table("web_payload") else {}
    for cid, syn in syn_of.items():
        for lp in paths.get(cid, ()):
            try:
                found = extract_payload((RAW / lp).read_text(encoding="utf-8", errors="replace"), syn)
            except OSError:
                found = None
            if found:
                yield cid, syn, found, "poc_file", lp
                break
        else:
            if cid in remote:
                yield cid, syn, *remote[cid][:1], "repo", remote[cid][1]
                continue
            for ref, text in evidence.get(cid, ()):
                if found := extract_payload(text, syn):
                    yield cid, syn, found, "advisory", ref
                    break
            else:
                if (description := descriptions.get(cid)) and (
                        found := extract_payload(description, syn)):
                    yield cid, syn, found, "description", cid
                elif cid in web:
                    yield cid, syn, *web[cid][:1], "web", web[cid][1]


def run(args):
    con = sqlite3.connect(DB)
    rows, canonicalized = [], 0
    for cid, syn, payload, source, origin in representative_payload(con):
        # Every later measurement reads this value (premise A3).
        canonical = matchertext.canon(payload)
        canonicalized += canonical != payload
        payload = canonical
        sk = skeleton(payload)
        if not sk or sk.strip("<idnqTAG /|;&=*+.:,-") == "":
            continue
        sk = group_skeleton(syn, payload)
        gid = hashlib.md5(f"{syn}\x00{sk}".encode()).hexdigest()[:12]
        rows.append((cid, syn, gid, sk, payload, source, origin))

    con.executescript("""
        DROP TABLE IF EXISTS syntactic_group;
        CREATE TABLE syntactic_group(cve_id TEXT PRIMARY KEY, syntax_type TEXT,
                                     group_id TEXT, skeleton TEXT, example TEXT,
                                     source TEXT, origin TEXT);
    """)
    con.executemany("INSERT OR REPLACE INTO syntactic_group VALUES(?,?,?,?,?,?,?)", rows)
    con.execute("CREATE INDEX idx_syngroup_gid ON syntactic_group(group_id)")
    con.commit()

    groups = con.execute("SELECT COUNT(DISTINCT group_id) FROM syntactic_group").fetchone()[0]
    print(f"syntactic groups: {len(rows)} CVEs -> {groups} distinct skeletons")
    print(f"payloads changed by canonicalization: {canonicalized}")
    print("fragments dropped as cut mid-expression: "
          + (", ".join(f"{s} {n}" for s, n in TRUNCATED.most_common()) or "none"))
    print("fragments completed with the closers that follow: "
          + (", ".join(f"{s} {n}" for s, n in EXTENDED.most_common()) or "none"))
    write("extraction_balance.csv", ["syntax_type", "dropped_cut", "completed"],
          [(s, TRUNCATED[s], EXTENDED[s]) for s in sorted(TRUNCATED.keys() | EXTENDED.keys())])
    write("canonicalization.csv", ["payloads", "changed"], [(len(rows), canonicalized)])
    for sk, n in con.execute("""SELECT skeleton, COUNT(*) c FROM syntactic_group
                                GROUP BY skeleton ORDER BY c DESC LIMIT 8"""):
        print(f"  {n:>5}  {sk[:70]}")
    con.close()


if __name__ == "__main__":
    run(argparse.ArgumentParser(description=__doc__).parse_args())
