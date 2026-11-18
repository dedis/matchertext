"""Stage 3b: sub-classify injection CVEs along orthogonal facets.

Each injection CVE is labelled independently on several dimensions rather than
pushed deeper into one tree, because the facets are independent (a SQLi is
union-based AND pre-auth AND MySQL AND data-exfil at once) and have very
different coverage. Results go to a long-format table:

    subclass(cve_id, dimension, label, method, confidence)

Dimensions and their strongest signal:
  privilege / attack_vector / user_interaction  <- CVSS vector (free, ~80%)
  technique                                     <- PoC payload syntax -> description phrase
  engine                                        <- payload dialect / product name
  injection_point / impact                      <- description phrase rules
  delivery                                      <- description phrase rule (HTML/DOM)
Only determined labels are stored; coverage is (rows in dimension)/(injection CVEs).
"""
import argparse
import re
import sqlite3
from pathlib import Path

from taxonomy import whole_document

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
# lower index = more authoritative source for the CVSS vector
CVSS_SRC = {"cna": 0, "adp_cisa": 1, "nvd": 2, "ghsa": 3, "redhat": 4, "nuclei": 5}
VER_RANK = {"4.0": 0, "3.1": 1, "3.0": 2, "2.0": 3}


def _rx(pairs):
    return [(re.compile(p, re.I | re.S), label) for label, p in pairs]


# --- content-derived facets: (label, regex), first match wins -----------------
# The technique of a CVE comes from its payload when the payload shows one: these regexes read attack syntax,
# and run on payloads only, since prose matches them too (a ';' or an '-o' in a sentence). Each list puts
# the more specific pattern first: a time delay before a command separator, which a delay payload also has.
TECHNIQUE = {
    "sql": _rx([
        ("union_based", r"\bunion\s+(?:all\s+)?select\b"),
        ("error_based", r"\bextractvalue\s*\(|\bupdatexml\s*\(|floor\s*\(\s*rand|\bconvert\s*\(.*\bint\b"),
        ("blind_time", r"\bsleep\s*\(|\bwaitfor\s+delay\b|\bpg_sleep\s*\(|\bbenchmark\s*\("),
        ("file_access", r"\binto\s+(?:out|dump)file\b|\bload_file\s*\(|\bxp_cmdshell\b"),
        ("stacked", r";\s*(?:drop|insert|update|delete|exec|create)\b"),
        # A tautology serves a login bypass and a boolean probe alike, so it names only itself.
        ("tautology", r"'\s*or\s*'?1'?\s*=\s*'?1|\bor\s+1\s*=\s*1\b|\badmin'\s*--"),
        ("blind_boolean", r"\band\s+\d+\s*=\s*\d+"),
    ]),
    "html_dom": _rx([
        # A DOM source marks DOM-based XSS; a sink that a payload calls (document.write, document.cookie) does not.
        ("dom_based", r"location\.(?:hash|search)|document\.(?:URL|referrer)|window\.name"),
    ]),
    "shell_command": _rx([
        ("substitution", r"\$\([^)]*\)|`[^`]+`"),
        ("blind", r"\bping\s+-c|\bsleep\s+\d"),
        ("separator", r"[;&|]\s*\w"),
        ("argument", r"(?:^|\s)--?[A-Za-z]"),
    ]),
    "code_eval": _rx([
        ("file_inclusion", r"\b(?:include|require)(?:_once)?\s*\("),
        ("deserialization", r"__reduce__|readObject|\bunserialize\s*\(|\bO:\d+:\""),
        ("eval", r"\beval\s*\(|\bassert\s*\(|\bsystem\s*\(|\bpassthru\s*\("),
    ]),
    "template": _rx([("sandbox_escape", r"__class__|__globals__|__subclasses__")]),
    "expression_language": _rx([("ognl_spel", r"#_memberAccess|@java\.|\bT\(")]),
    "ldap": _rx([("filter_breakout", r"\*\)\(|\)\(\w+=")]),
    "xpath_xquery": _rx([("filter_breakout", r"'?\s*or\s*'?1'?=")]),
    "crlf_header": _rx([("response_splitting", r"%0d%0a.*%0d%0a")]),
    "formula_csv": _rx([("dde", r"=cmd\|"), ("hyperlink", r"=HYPERLINK")]),
    "nosql": _rx([("operator", r"\$(?:where|ne|gt|regex)")]),
    "xml": _rx([("xxe", r"<!ENTITY|<!DOCTYPE")]),
}

# Otherwise the description names the technique. A bare "blind" names no kind of blind attack, so it is its own
# label. A trailing "." is the default of a syntax whose technique the description need not name.
TECHNIQUE_TEXT = {
    "sql": _rx([
        ("union_based", r"\bunion[- ]based\b|\bunion\s+(?:all\s+)?select\b"),
        ("error_based", r"\berror[- ]based\b"),
        ("blind_time", r"\btime[- ]based\b"),
        ("blind_boolean", r"\bboolean[- ]based\b"),
        ("stacked", r"\bstacked quer(?:y|ies)\b"),
        ("auth_bypass", r"\b(?:authentication|login|auth) bypass\b|\bbypass(?:es|ing)? (?:the )?(?:authentication|login)\b"),
        ("blind", r"\bblind\b"),
    ]),
    "html_dom": _rx([
        ("dom_based", r"\bDOM[- ]based\b|location\.(?:hash|search)|document\.(?:URL|referrer)|window\.name"),
        ("mutation", r"\bmutation\b|\bmXSS\b"),
        ("stored", r"\bstored\b|(?<!non-)(?<!non )\bpersistent\b|\bsecond[- ]order\b"),
        ("reflected", r"\breflected\b|\bnon[- ]persistent\b"),
    ]),
    "shell_command": _rx([("blind", r"\bblind\b")]),
    "code_eval": _rx([
        ("file_inclusion", r"\b(?:remote|local) file inclusion\b|\bRFI\b|\bLFI\b"),
        ("deserialization", r"\bdeserial|\bunserialize\b|\bgadget\b"),
        ("eval", r"\beval\b"),
    ]),
    "template": _rx([("sandbox_escape", r"\bsandbox\b"), ("ssti", r".")]),
    "expression_language": _rx([("ognl_spel", r"\bognl\b|\bspel\b"), ("el", r".")]),
    "ldap": _rx([("blind", r"\bblind\b")]),
    "xpath_xquery": _rx([("blind", r"\bblind\b")]),
    "crlf_header": _rx([("response_splitting", r"response splitting"), ("header_injection", r".")]),
    "formula_csv": _rx([("dde", r"\bDDE\b"), ("macro", r".")]),
    "nosql": _rx([("js_injection", r".")]),
    "xml": _rx([("xxe", r"\bXXE\b|external entit"), ("xml_injection", r".")]),
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
    ("stored", r"\bstored\b|\bsecond[- ]order\b|(?<!non-)(?<!non )\bpersistent\b"),
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
    vectors = best_vectors(con)
    # The payload syntactic_group chose for the CVE, so both stages read one value.
    payload_of = dict(con.execute("SELECT cve_id, example FROM syntactic_group"))

    rows = []
    for cid, syn in syn_of.items():
        desc = desc_of.get(cid, "")
        payload = payload_of.get(cid)
        blob = f"{payload or ''} {desc}"

        for dim, label in cvss_facets(vectors.get(cid, "")).items():
            rows.append((cid, dim, label, "cvss", None))

        tech = first(TECHNIQUE.get(syn, ()), payload) if payload else None
        if tech:
            rows.append((cid, "technique", tech, "payload", None))
        elif (tech := first(TECHNIQUE_TEXT.get(syn, ()), desc)):
            rows.append((cid, "technique", tech, "rule", None))

        eng = first(ENGINE.get(ENGINE_GROUP.get(syn, syn), ()), blob)
        if eng:
            rows.append((cid, "engine", eng, "payload" if payload and first(
                ENGINE.get(ENGINE_GROUP.get(syn, syn), ()), payload) else "rule", None))
        for dim, rules in (("injection_point", INJECTION_POINT), ("impact", IMPACT)):
            lbl = first(rules, desc)
            if lbl:
                rows.append((cid, dim, lbl, "rule", None))
        if syn == "html_dom" and whole_document(desc):
            rows.append((cid, "delivery", "whole_document", "rule", None))

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
