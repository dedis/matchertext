"""Stage 3b: sub-classify injection CVEs along orthogonal facets.

Each injection CVE is labelled independently on several dimensions rather than
pushed deeper into one tree, because the facets are independent (a SQLi is
union-based AND pre-auth AND MySQL AND data-exfil at once) and have very
different coverage. Results go to a long-format table:

    subclass(cve_id, dimension, label, method, confidence)

Dimensions and their strongest signal:
  privilege / attack_vector / user_interaction  <- CVSS vector (free, ~80%)
  technique                                     <- PoC payload -> description
  engine                                        <- payload dialect / product name
  injection_point / impact                      <- description phrase rules
Only determined labels are stored; coverage is (rows in dimension)/(injection CVEs).
"""
import argparse
import re
import sqlite3
from collections import defaultdict
from pathlib import Path

from report import extract_payload

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
RAW = ROOT / "data" / "raw"
# lower index = more authoritative source for the CVSS vector
CVSS_SRC = {"cna": 0, "adp_cisa": 1, "nvd": 2, "ghsa": 3, "redhat": 4, "nuclei": 5}
VER_RANK = {"4.0": 0, "3.1": 1, "3.0": 2, "2.0": 3}


def _rx(pairs):
    return [(re.compile(p, re.I | re.S), label) for label, p in pairs]


# --- content-derived facets: (label, regex), first match wins -----------------
TECHNIQUE = {
    "sql": _rx([
        ("union_based", r"\bunion\s+(?:all\s+)?select\b"),
        ("error_based", r"\bextractvalue\s*\(|\bupdatexml\s*\(|floor\s*\(\s*rand|\bconvert\s*\(.*\bint\b"),
        ("blind_time", r"\bsleep\s*\(|\bwaitfor\s+delay\b|\bpg_sleep\s*\(|\bbenchmark\s*\("),
        ("file_access", r"\binto\s+(?:out|dump)file\b|\bload_file\s*\(|\bxp_cmdshell\b"),
        ("stacked", r";\s*(?:drop|insert|update|delete|exec|create)\b"),
        ("auth_bypass", r"'\s*or\s*'?1'?\s*=\s*'?1|\bor\s+1\s*=\s*1\b|\badmin'\s*--"),
        ("blind_boolean", r"\band\s+\d+\s*=\s*\d+|\bboolean-based\b|\bblind\b"),
    ]),
    "html_dom": _rx([
        ("dom_based", r"\bDOM[- ]based\b|document\.(?:location|write|cookie)|\.innerHTML|location\.hash"),
        ("mutation", r"\bmutation\b|\bmXSS\b"),
        ("stored", r"\bstored\b|\bpersistent\b|\bsecond[- ]order\b"),
        ("reflected", r"\breflected\b|\bnon[- ]persistent\b"),
    ]),
    "shell_command": _rx([
        ("substitution", r"\$\([^)]*\)|`[^`]+`"),
        ("separator", r"[;&|]\s*\w"),
        ("argument", r"(?:^|\s)--?[A-Za-z]"),
        ("blind", r"\bblind\b|ping\s+-c|sleep\s+\d"),
    ]),
    "code_eval": _rx([
        ("file_inclusion", r"\b(?:remote|local) file inclusion\b|\bRFI\b|\bLFI\b|\binclude\s*\("),
        ("deserialization", r"\bdeserial|\bunserialize\b|\bgadget\b|__reduce__|readObject"),
        ("eval", r"\beval\s*\(|\bassert\s*\(|\bsystem\s*\(|\bpassthru\s*\("),
    ]),
    "template": _rx([("sandbox_escape", r"\bsandbox\b|__class__|__globals__"), ("ssti", r".")]),
    "expression_language": _rx([("ognl_spel", r"\bognl\b|\bspel\b|T\("), ("el", r".")]),
    "ldap": _rx([("filter_breakout", r"\*\)\(|\)\(\w+="), ("blind", r"\bblind\b")]),
    "xpath_xquery": _rx([("blind", r"\bblind\b"), ("filter_breakout", r"'?\s*or\s*'?1'?=")]),
    "crlf_header": _rx([("response_splitting", r"response splitting|%0d%0a.*%0d%0a"),
                        ("header_injection", r".")]),
    "formula_csv": _rx([("dde", r"=cmd\||\bDDE\b"), ("hyperlink", r"=HYPERLINK"), ("macro", r".")]),
    "nosql": _rx([("operator", r"\$(?:where|ne|gt|regex)"), ("js_injection", r".")]),
    "xml": _rx([("xxe", r"<!ENTITY|<!DOCTYPE"), ("xml_injection", r".")]),
    "argument": _rx([("argument", r".")]),
}

ENGINE = {
    "sql": _rx([
        ("mssql", r"\bwaitfor\s+delay\b|xp_cmdshell|@@version|\bMS[- ]?SQL\b|SQL Server"),
        ("oracle", r"\bdbms_\w+|\butl_\w+|\brownum\b|\bOracle\b"),
        ("postgres", r"\bpg_sleep\b|::\w+|\bPostgre|\bPostgreSQL\b"),
        ("mysql", r"\bsleep\s*\(|information_schema|\bMySQL\b|\bMariaDB\b"),
        ("sqlite", r"\bsqlite"),
    ]),
    "template": _rx([("jinja", r"jinja|\{\{.*\}\}.*flask|\bflask\b"), ("freemarker", r"freemarker"),
                     ("velocity", r"velocity"), ("twig", r"\btwig\b"), ("smarty", r"smarty"),
                     ("thymeleaf", r"thymeleaf"), ("handlebars", r"handlebars"), ("erb", r"\berb\b")]),
    "expression_language": _rx([("ognl", r"\bognl\b|struts"), ("spel", r"\bspel\b|spring"),
                                ("mvel", r"\bmvel\b")]),
    "shell_command": _rx([("windows", r"calc\.exe|cmd(?:\.exe|\s*/c)|powershell|C:\\"),
                          ("unix", r"/bin/(?:sh|bash)|/etc/passwd|;\s*id\b|/tmp/")]),
    "formula_csv": _rx([("windows", r"calc\.exe|cmd\|"), ("unix", r"/bin/")]),
}
ENGINE_GROUP = {"expression_language": "expression_language"}  # else group == syntax

INJECTION_POINT = _rx([
    ("stored", r"\bstored\b|\bsecond[- ]order\b|\bpersistent\b"),
    ("http_header", r"\bheader\b|user[- ]agent|\breferer\b|x-forwarded"),
    ("cookie", r"\bcookie\b"),
    ("order_by", r"\border\s+by\b|\bgroup\s+by\b"),
    ("request_body", r"request body|\bJSON\b|\bPOST data\b"),
    ("url_path", r"\bURL\b|\bpath\b|\bURI\b|\bendpoint\b"),
    ("parameter", r"\bparameter\b|\bparam\b|\bfield\b|\bargument\b|\bGET\b|\bquery string\b"),
])

IMPACT = _rx([
    ("rce", r"remote code execution|arbitrary (?:code|command)|command execution|\bRCE\b"),
    ("auth_bypass", r"authentication bypass|bypass authentication|\bauth bypass\b|login bypass"),
    ("file_write", r"write.*arbitrary file|upload.*(?:shell|webshell)|arbitrary file (?:write|upload)"),
    ("file_read", r"arbitrary file (?:read|disclosure)|read.*arbitrary file|\bLFI\b"),
    ("data_exfil", r"\bextract\b|\bdump\b|disclos|exfiltrat|sensitive (?:data|information)|steal"),
    ("priv_esc", r"privilege escalation|escalate privileges|gain (?:admin|root)"),
    ("dos", r"denial of service|\bDoS\b|crash|infinite loop"),
])


def first(rules, text):
    for rx, label in rules:
        if rx.search(text):
            return label
    return None


def best_vectors(con):
    best = {}  # cve -> (rank_key, vector)
    for cid, vector, source in con.execute(
            "SELECT cve_id, vector, source FROM cvss WHERE vector IS NOT NULL"):
        ver = next((v for v in VER_RANK if v in (vector or "")), None) or \
            ("2.0" if "AV:" in vector and not vector.startswith("CVSS") else "9")
        key = (CVSS_SRC.get(source, 9), VER_RANK.get(ver, 9))
        if cid not in best or key < best[cid][0]:
            best[cid] = (key, vector)
    return {c: v for c, (_, v) in best.items()}


def cvss_facets(vector):
    f = {}
    for kv in vector.split("/"):
        if ":" in kv and not kv.startswith("CVSS"):
            k, _, v = kv.partition(":")
            f[k] = v
    out = {}
    pr, au = f.get("PR"), f.get("Au")
    if pr:
        out["privilege"] = {"N": "pre_auth", "L": "low_priv", "H": "high_priv"}.get(pr)
    elif au:
        out["privilege"] = "pre_auth" if au == "N" else "authenticated"
    av = f.get("AV")
    if av:
        out["attack_vector"] = {"N": "network", "A": "adjacent", "L": "local", "P": "physical"}.get(av)
    ui = f.get("UI")
    if ui:
        out["user_interaction"] = "none" if ui == "N" else "required"
    return {k: v for k, v in out.items() if v}


def run(args):
    con = sqlite3.connect(DB)
    syn_of = dict(con.execute("SELECT cve_id, syntax_type FROM classification"))
    desc_of = dict(con.execute("""SELECT cve_id, COALESCE(description, '') FROM cve
                                  WHERE cve_id IN (SELECT cve_id FROM classification)"""))
    poc_paths = defaultdict(list)
    # Ordered so the representative payload per CVE is a property of the data,
    # not of physical row order: without it, ingesting a new PoC source silently
    # reshuffles which file each CVE's payload is extracted from.
    for cid, lp in con.execute("""SELECT cve_id, local_path FROM poc
                                  WHERE local_path IS NOT NULL
                                  ORDER BY cve_id, source, ref"""):
        if cid in syn_of:
            poc_paths[cid].append(lp)
    vectors = best_vectors(con)
    # Same fallback as syntactic_group: local file first, then a payload
    # recovered from a linked GitHub repo.
    remote = dict(con.execute(
        "SELECT cve_id, payload FROM remote_payload WHERE payload IS NOT NULL")
        if con.execute("""SELECT COUNT(*) FROM sqlite_master
                          WHERE type='table' AND name='remote_payload'""").fetchone()[0]
        else ())

    rows = []
    for cid, syn in syn_of.items():
        desc = desc_of.get(cid, "")
        payload = None
        for lp in poc_paths.get(cid, ()):
            try:
                payload = extract_payload((RAW / lp).read_text(encoding="utf-8", errors="replace"), syn)
            except OSError:
                payload = None
            if payload:
                break
        payload = payload or remote.get(cid)
        blob = f"{payload or ''} {desc}"

        for dim, label in cvss_facets(vectors.get(cid, "")).items():
            rows.append((cid, dim, label, "cvss", None))

        tech = first(TECHNIQUE.get(syn, ()), payload) if payload else None
        if tech:
            rows.append((cid, "technique", tech, "payload", None))
        elif (tech := first(TECHNIQUE.get(syn, ()), desc)):
            rows.append((cid, "technique", tech, "rule", None))

        eng = first(ENGINE.get(ENGINE_GROUP.get(syn, syn), ()), blob)
        if eng:
            rows.append((cid, "engine", eng, "payload" if payload and first(
                ENGINE.get(ENGINE_GROUP.get(syn, syn), ()), payload) else "rule", None))
        for dim, rules in (("injection_point", INJECTION_POINT), ("impact", IMPACT)):
            lbl = first(rules, desc)
            if lbl:
                rows.append((cid, dim, lbl, "rule", None))

    con.executescript("""
        DROP TABLE IF EXISTS subclass;
        CREATE TABLE subclass(cve_id TEXT, dimension TEXT, label TEXT, method TEXT,
                              confidence REAL, UNIQUE(cve_id, dimension, label));
    """)
    con.executemany("INSERT OR IGNORE INTO subclass VALUES(?,?,?,?,?)", rows)
    con.execute("CREATE INDEX idx_subclass_cve ON subclass(cve_id)")
    con.commit()
    total = len(syn_of)
    print(f"subclass: {len(rows)} labels over {total} injection CVEs")
    for dim, n in con.execute("""SELECT dimension, COUNT(DISTINCT cve_id) FROM subclass
                                 GROUP BY 1 ORDER BY 2 DESC"""):
        print(f"  {dim}: {n} ({n / total:.0%})")
    con.close()


if __name__ == "__main__":
    run(argparse.ArgumentParser(description=__doc__).parse_args())
