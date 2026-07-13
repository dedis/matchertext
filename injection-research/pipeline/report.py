"""Build a markdown report of the classification results, previewing each
category with real injection payloads extracted from linked Exploit-DB entries.

Payloads are heuristically extracted from exploit files via per-syntax
signatures; sampling is seeded, so the report is reproducible.

Usage: python3 pipeline/report.py [--samples 5] [--seed 42]
"""
import argparse
import random
import re
import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
RAW = ROOT / "data" / "raw"
OUT = ROOT / "data" / "exports" / "report.md"

# Per-syntax payload signatures, ordered most-specific first. Applied to the
# text of a linked exploit file to pull out the actual injection string.
_SIGNS = {
    "sql": [r"UNION(?:\s+ALL)?\s+SELECT\b[^\n]*", r"'\s*(?:OR|AND)\s*'?\d+'?\s*=\s*'?\d+[^\n]*",
            r"'\s*(?:OR|AND)\b[^\n]*--", r"\bAND\s+\d+=\d+[^\n]*", r"';?\s*WAITFOR\s+DELAY[^\n]*",
            r"\bextractvalue\s*\([^\n]*", r"\bSLEEP\s*\(\d+\)[^\n]*", r"\[sql\]"],
    "html_dom": [r"<script\b[^>]*>.*?</script>", r"<img[^>]*onerror\s*=[^\n]*?>",
                 r"<svg[^>]*on\w+\s*=[^\n]*?>", r'"><script[^\n]*', r"javascript:\S+",
                 r"%3[Cc]script[^\n]*"],
    "shell_command": [r"[;&|]\s*(?:cat|id|whoami|ping|curl|wget|sleep|uname|nc|bash|sh)\b[^\n]*",
                      r"\$\([^)\n]+\)", r"`[^`\n]+`"],
    "code_eval": [r"<\?php\b[^\n]*", r"\beval\s*\([^\n)]+\)[^\n]*", r"\bsystem\s*\([^\n)]+\)[^\n]*",
                  r"\bpassthru\s*\([^\n)]+\)[^\n]*", r"\bassert\s*\([^\n)]+\)[^\n]*"],
    "crlf_header": [r"(?:%0[dD]%0[aA]|\\r\\n)\S+"],
    "ldap": [r"\*\)\([^\n]*", r"\)\(\w+=[^\n]*"],
    "xpath_xquery": [r"'?\s*or\s*'?1'?\s*=\s*'?1[^\n]*", r"count\(/[^\n]*", r"//\*[^\n]*"],
    "template": [r"\{\{[^\n]*?\}\}", r"<%[^\n]*?%>", r"#\{[^\n]*?\}"],
    "expression_language": [r"\$\{[^\n]*?\}", r"#\{[^\n]*?\}", r"%\{[^\n]*?\}"],
    "formula_csv": [r"[=+@\-](?:cmd|CMD|SUM|HYPERLINK|WEBSERVICE)[^\n]*", r"=\d+[+\-*][^\n]*"],
    "nosql": [r"\{\s*[\"']?\$(?:gt|ne|where|regex)[^\n]*", r"\[\$ne\][^\n]*"],
    "xml": [r"<!DOCTYPE[^\n]*\[[^\n]*", r"<!ENTITY\b[^\n]*", r"SYSTEM\s+[\"']file:[^\n]*"],
    "argument": [r"-[A-Za-z][\w-]*=`[^\n]*", r"-o\w+=\S[^\n]*", r"-[A-Za-z][\w-]*=\S+"],
}
SIGNS = {s: [re.compile(p, re.I | re.S) for p in ps] for s, ps in _SIGNS.items()}


def extract_payload(text, syn):
    for rx in SIGNS.get(syn, ()):
        m = rx.search(text)
        if m:
            frag = " ".join(m.group(0).split())
            if 3 <= len(frag) <= 200:
                return frag
    return None


def sample_payloads(con, syn, n, rng):
    links = con.execute(
        """SELECT DISTINCT c.cve_id, p.source, p.local_path FROM poc p
           JOIN classification c USING(cve_id)
           WHERE c.syntax_type=? AND p.local_path IS NOT NULL""", (syn,)).fetchall()
    rng.shuffle(links)
    out, seen = [], set()
    for cve_id, source, rel in links:
        try:
            payload = extract_payload((RAW / rel).read_text(encoding="utf-8", errors="replace"), syn)
        except OSError:
            continue
        if payload and payload not in seen:
            seen.add(payload)
            out.append((cve_id, source, payload))
            if len(out) >= n:
                break
    return out, len(links)


def table(out, header, rows):
    out.append("| " + " | ".join(header) + " |")
    out.append("|" + "---|" * len(header))
    out.extend("| " + " | ".join(str(c) for c in r) + " |" for r in rows)
    out.append("")


def md_escape(s):
    return s.replace("|", "\\|").replace("`", "\\`")


def run(args):
    rng = random.Random(args.seed)
    con = sqlite3.connect(DB)
    q = lambda sql, *p: con.execute(sql, p).fetchall()
    out = ["# CVE injection classification report", ""]

    total = q("SELECT COUNT(*) FROM cve WHERE state='PUBLISHED'")[0][0]
    related = q("SELECT COUNT(*) FROM classification")[0][0]
    with_poc = q("""SELECT COUNT(DISTINCT cve_id) FROM poc
                    WHERE cve_id IN (SELECT cve_id FROM classification)""")[0][0]
    out += [f"Published CVEs: **{total:,}** — injection-related: **{related:,}** "
            f"({related / total:.1%}); with a linked proof-of-concept: **{with_poc:,}**", ""]

    out.append("## Label attribution")
    table(out, ["method", "count", "share"],
          [(m, f"{n:,}", f"{n / related:.1%}") for m, n in
           q("SELECT method, COUNT(*) FROM classification GROUP BY 1 ORDER BY 2 DESC")])

    out.append("## PoC sources (injection-related CVEs)")
    table(out, ["source", "poc rows", "distinct CVEs", "injection CVEs"],
          [(s, f"{n:,}", f"{d:,}", f"{i:,}") for s, n, d, i in
           q("""SELECT p.source, COUNT(*), COUNT(DISTINCT p.cve_id), COUNT(DISTINCT c.cve_id)
                FROM poc p LEFT JOIN classification c USING(cve_id)
                GROUP BY 1 ORDER BY 2 DESC""")])

    out.append("## Weakness families")
    table(out, ["family", "count", "kev", "share of injection"],
          [(f, f"{n:,}", k, f"{n / related:.1%}") for f, n, k in
           q("""SELECT c.weakness_family, COUNT(*), COUNT(k.cve_id) FROM classification c
                LEFT JOIN kev k USING(cve_id) GROUP BY 1 ORDER BY 2 DESC""")])

    out.append("## Syntax types")
    table(out, ["syntax", "count", "kev", "with PoC", "share of injection"],
          [(s, f"{n:,}", k, p, f"{n / related:.1%}") for s, n, k, p in
           q("""SELECT c.syntax_type, COUNT(*), COUNT(DISTINCT k.cve_id),
                       COUNT(DISTINCT e.cve_id)
                FROM classification c LEFT JOIN kev k USING(cve_id)
                LEFT JOIN poc e USING(cve_id) GROUP BY 1 ORDER BY 2 DESC""")])

    out.append("## Payload samples per syntax type")
    out.append("")
    out.append("Injection strings extracted from linked proof-of-concept files "
               "(Exploit-DB, Nuclei, Metasploit; heuristic — the CVE column links "
               "the payload to its record, the source column to the PoC database).")
    out.append("")
    for syn, in q("SELECT DISTINCT syntax_type FROM classification ORDER BY 1"):
        samples, n_links = sample_payloads(con, syn, args.samples, rng)
        out.append(f"### {syn} ({n_links:,} extractable PoCs)")
        out.append("")
        if not samples:
            out.append("_no payload extracted from linked PoCs_")
            out.append("")
            continue
        table(out, ["cve", "payload", "source"],
              [(cve, "`" + md_escape(p) + "`", source) for cve, source, p in samples])

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(out) + "\n")
    print(f"wrote {OUT}")
    con.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--samples", type=int, default=5, help="payload examples per category")
    ap.add_argument("--seed", type=int, default=42, help="sampling seed")
    run(ap.parse_args())
