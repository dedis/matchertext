"""Build a self-contained HTML report of the classification results.

Renders the classification, PoC linkage, syntactic groups, matchertext-prevention
analysis, and sub-classification into a single styled, theme-aware HTML file
(data/exports/report.html). Payloads are heuristically extracted from linked PoC
files; payload sampling is seeded, so the report is reproducible.

Usage: python3 pipeline/report.py [--samples 5] [--seed 42]
"""
import argparse
import html
import random
import re
import sqlite3
from pathlib import Path

import matchertext

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
RAW = ROOT / "data" / "raw"
OUT = ROOT / "data" / "exports" / "report.html"

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


# --- HTML helpers -------------------------------------------------------------
def esc(s):
    return html.escape(str(s))


def co(s):
    return f"<code>{esc(s)}</code>"


def pill(s):
    return f'<span class="pill">{esc(s)}</span>'


def bar(frac):
    return f'<span class="bar"><span style="width:{min(frac, 1) * 100:.1f}%"></span></span>'


def tbl(headers, rows, aligns=None):
    aligns = aligns or [""] * len(headers)
    head = "".join(f'<th class="{a}">{esc(h)}</th>' for h, a in zip(headers, aligns))
    body = []
    for r in rows:
        cells = "".join(f'<td class="{a}">{c}</td>' for c, a in zip(r, aligns))
        body.append(f"<tr>{cells}</tr>")
    return (f'<div class="tw"><table><thead><tr>{head}</tr></thead>'
            f'<tbody>{"".join(body)}</tbody></table></div>')


def section(sid, title, *blocks):
    return f'<section id="{sid}"><h2>{esc(title)}</h2>{"".join(blocks)}</section>'


def p(text):
    return f"<p>{text}</p>"


# --- content sections ---------------------------------------------------------
def label_attribution(q, related):
    rows = [(esc(m), f"{n:,}", f"{n / related:.1%}", bar(n / related))
            for m, n in q("SELECT method, COUNT(*) FROM classification GROUP BY 1 ORDER BY 2 DESC")]
    return section("attribution", "Label attribution",
                   p("How each injection CVE's syntax type was determined — CWE map, "
                     "phrase rules, naive-Bayes fallback, or left unknown."),
                   tbl(["method", "count", "share", ""], rows, ["", "num", "num", "barcol"]))


def poc_sources(q):
    rows = [(pill(s), f"{n:,}", f"{d:,}", f"{i:,}") for s, n, d, i in q(
        """SELECT p.source, COUNT(*), COUNT(DISTINCT p.cve_id), COUNT(DISTINCT c.cve_id)
           FROM poc p LEFT JOIN classification c USING(cve_id) GROUP BY 1 ORDER BY 2 DESC""")]
    return section("poc-sources", "Proof-of-concept sources",
                   p("PoC references linked to injection CVEs, by source database."),
                   tbl(["source", "PoC rows", "distinct CVEs", "injection CVEs"], rows,
                       ["", "num", "num", "num"]))


def families(q, related):
    rows = [(esc(f), f"{n:,}", f"{k:,}", f"{n / related:.1%}", bar(n / related)) for f, n, k in q(
        """SELECT c.weakness_family, COUNT(*), COUNT(k.cve_id) FROM classification c
           LEFT JOIN kev k USING(cve_id) GROUP BY 1 ORDER BY 2 DESC""")]
    return section("families", "Weakness families",
                   tbl(["family", "count", "KEV", "share", ""], rows,
                       ["", "num", "num", "num", "barcol"]))


def syntax_types(q, related):
    rows = [(pill(s), f"{n:,}", f"{k:,}", f"{pc:,}", f"{n / related:.1%}", bar(n / related))
            for s, n, k, pc in q(
        """SELECT c.syntax_type, COUNT(*), COUNT(DISTINCT k.cve_id), COUNT(DISTINCT e.cve_id)
           FROM classification c LEFT JOIN kev k USING(cve_id)
           LEFT JOIN poc e USING(cve_id) GROUP BY 1 ORDER BY 2 DESC""")]
    return section("syntax", "Syntax types",
                   p("The embedded language each injection targets — the primary axis."),
                   tbl(["syntax", "count", "KEV", "with PoC", "share", ""], rows,
                       ["", "num", "num", "num", "num", "barcol"]))


def syntactic_groups(q, groups):
    grouped = sum(n for *_, n in groups)
    singletons = sum(1 for *_, n in groups if n == 1)
    top = tbl(["size", "syntax", "skeleton", "example"],
              [(f"{n:,}", pill(s), co(sk), co(ex)) for s, sk, ex, n in groups[:14]],
              ["num", "", "", ""])
    seen, per_syn = set(), []
    for s, sk, ex, n in groups:
        if s not in seen:
            seen.add(s)
            per_syn.append((pill(s), f"{n:,}", co(sk), co(ex)))
    largest = tbl(["syntax", "size", "largest skeleton", "example"], sorted(per_syn), ["", "num", "", ""])
    return section("groups", "Syntactic groups",
                   p(f"PoC-backed CVEs bucketed by an exact <em>syntactic skeleton</em> — the "
                     f"payload with literals, numbers and identifiers abstracted to "
                     f"{co('<q> <n> <id>')} and only the structural grammar (matchers, "
                     f"operators, tags, keywords) kept. <strong>{grouped:,}</strong> CVEs fall "
                     f"into <strong>{len(groups):,}</strong> distinct skeletons "
                     f"({singletons:,} singletons)."),
                   "<h3>Largest groups</h3>", top,
                   "<h3>Largest skeleton per syntax type</h3>", largest)


def matchertext_section(q, groups, prev_cves, total_cves):
    rows = []
    for s, sk, ex, n in groups[:20]:
        rewrite, prevent = matchertext.assess(s, sk)
        verdict = (f'<span class="badge yes">prevents</span> {esc(prevent)}' if prevent
                   else '<span class="badge no">no</span>')
        cls = "prevent" if prevent else "noprevent"
        rows.append((f"{n:,}", pill(s), co(sk), co(rewrite), verdict, cls))
    body = "".join(
        "<tr class='{}'>".format(r[5]) +
        "".join(f'<td class="{a}">{c}</td>' for c, a in
                zip(r[:5], ["num", "", "skel", "skel", "verdict"])) + "</tr>"
        for r in rows)
    table = (f'<div class="tw"><table><thead><tr>'
             f'<th class="num">size</th><th>syntax</th><th>skeleton</th>'
             f'<th>matchertext rewrite</th><th>matchertext preventable?</th>'
             f'</tr></thead><tbody>{body}</tbody></table></div>')
    callout = (f'<div class="callout"><div class="big">{prev_cves / total_cves:.0%}</div>'
               f'<div>of the <strong>{total_cves:,}</strong> grouped CVEs '
               f'(<strong>{prev_cves:,}</strong>) fall in a matchertext-preventable skeleton</div></div>')
    return section("matchertext", "Matchertext prevention by skeleton",
                   p("Assuming the host adopts a matchertext syntax that delimits the "
                     f"untrusted value with a matcher pair (e.g. {co('[…]')} for SQL, as in the "
                     "paper), each skeleton is rewritten to its matchertext-equivalent — "
                     f"unmatched matchers escaped via ToMatchertext ({co('\\o()')} / "
                     f"{co('\\c()')}) — and checked for breakout (paper §4.7)."),
                   callout,
                   p("A skeleton is <strong>not</strong> preventable (grey) when the sink "
                     "executes the contained value by design (templates, expression "
                     "languages, eval, spreadsheet formulas) or the breakout uses "
                     "non-matchers with no matcher hosting (shell metacharacters, CR/LF, "
                     "XML/XXE angle brackets)."),
                   table)


def subclass_section(q, related):
    cov = tbl(["dimension", "labeled CVEs", "coverage", "labels", ""],
              [(esc(d), f"{n:,}", f"{n / related:.1%}", k, bar(n / related)) for d, n, k in q(
                  """SELECT dimension, COUNT(DISTINCT cve_id), COUNT(DISTINCT label)
                     FROM subclass GROUP BY 1 ORDER BY 2 DESC""")],
              ["", "num", "num", "num", "barcol"])
    dists = []
    for dim, title in (("privilege", "Privilege required (CVSS)"),
                       ("attack_vector", "Attack vector (CVSS)"),
                       ("impact", "Impact (description)")):
        drows = q("SELECT label, COUNT(*) FROM subclass WHERE dimension=? GROUP BY 1 ORDER BY 2 DESC", dim)
        tot = sum(n for _, n in drows) or 1
        dists.append(f"<h3>{esc(title)}</h3>" + tbl(
            ["label", "count", ""], [(esc(l), f"{n:,}", bar(n / tot)) for l, n in drows],
            ["", "num", "barcol"]))
    tech = ["<h3>Technique by syntax type</h3>"]
    for syn, in q("""SELECT DISTINCT c.syntax_type FROM classification c JOIN subclass s
                     USING(cve_id) WHERE s.dimension='technique' ORDER BY 1"""):
        techs = q("""SELECT s.label, COUNT(*) FROM subclass s JOIN classification c USING(cve_id)
                     WHERE s.dimension='technique' AND c.syntax_type=? GROUP BY 1 ORDER BY 2 DESC""", syn)
        chips = " ".join(f'<span class="chip">{esc(l)} <b>{n:,}</b></span>' for l, n in techs)
        tech.append(f'<div class="techrow"><span class="pill">{esc(syn)}</span> {chips}</div>')
    return section("subclass", "Sub-classification",
                   p("Injection CVEs labelled on orthogonal facets (a CVE can carry one label "
                     "per dimension). Coverage is the share of injection CVEs a facet could be "
                     "determined for; dimensions differ because their signals do."),
                   cov, *dists, *tech)


def payload_samples(con, q, rng, n):
    blocks = [p("Real injection strings extracted from linked PoC files (Exploit-DB, Nuclei, "
                "Metasploit; heuristic). The CVE links the payload to its record.")]
    for syn, in q("SELECT DISTINCT syntax_type FROM classification ORDER BY 1"):
        samples, n_links = sample_payloads(con, syn, n, rng)
        blocks.append(f'<h3>{esc(syn)} <span class="mut">· {n_links:,} extractable PoCs</span></h3>')
        if not samples:
            blocks.append(p('<span class="mut">no payload extracted from linked PoCs</span>'))
            continue
        blocks.append(tbl(["CVE", "payload", "source"],
                          [(esc(cve), co(pl), pill(src)) for cve, src, pl in samples]))
    return section("payloads", "Payload samples per syntax type", *blocks)


NAV = [("attribution", "Attribution"), ("poc-sources", "PoC sources"), ("families", "Families"),
       ("syntax", "Syntax"), ("groups", "Syntactic groups"), ("matchertext", "Matchertext"),
       ("subclass", "Sub-classes"), ("payloads", "Payloads")]


def run(args):
    rng = random.Random(args.seed)
    con = sqlite3.connect(DB)
    q = lambda sql, *pp: con.execute(sql, pp).fetchall()

    total = q("SELECT COUNT(*) FROM cve WHERE state='PUBLISHED'")[0][0]
    related = q("SELECT COUNT(*) FROM classification")[0][0]
    with_poc = q("""SELECT COUNT(DISTINCT cve_id) FROM poc
                    WHERE cve_id IN (SELECT cve_id FROM classification)""")[0][0]
    groups = q("""SELECT syntax_type, skeleton, MIN(example), COUNT(*) c
                  FROM syntactic_group GROUP BY group_id ORDER BY c DESC""")
    grouped = sum(n for *_, n in groups)
    prev_cves = sum(n for s, sk, _e, n in groups if matchertext.assess(s, sk)[1])

    kpis = "".join(
        f'<div class="kpi"><div class="kv">{v}</div><div class="kl">{l}</div>'
        f'<div class="ks">{s}</div></div>'
        for v, l, s in (
            (f"{total:,}", "published CVEs", "MITRE cvelistV5"),
            (f"{related:,}", "injection-related", f"{related / total:.1%} of all CVEs"),
            (f"{with_poc:,}", "with a PoC", "linked exploit / template"),
            (f"{prev_cves / grouped:.0%}", "matchertext-preventable",
             f"{prev_cves:,} of {grouped:,} grouped")))
    nav = "".join(f'<a href="#{sid}">{esc(t)}</a>' for sid, t in NAV)

    body = "".join([
        f'<header><h1>CVE injection classification</h1>'
        f'<p class="sub">Syntactic classification of injection vulnerabilities across the '
        f'public CVE corpus, with matchertext-prevention analysis.</p>'
        f'<div class="kpis">{kpis}</div></header>',
        f'<nav class="toc">{nav}</nav>',
        label_attribution(q, related),
        poc_sources(q),
        families(q, related),
        syntax_types(q, related),
        syntactic_groups(q, groups),
        matchertext_section(q, groups, prev_cves, grouped),
        subclass_section(q, related),
        payload_samples(con, q, rng, args.samples),
    ])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(f"<!doctype html><html lang=en><head><meta charset=utf-8>"
                   f'<meta name=viewport content="width=device-width,initial-scale=1">'
                   f"<title>CVE injection classification</title><style>{CSS}</style></head>"
                   f'<body><main class="wrap">{body}</main></body></html>')
    print(f"wrote {OUT}")
    con.close()


CSS = """
*{box-sizing:border-box}
:root{--bg:#f6f8fb;--panel:#fff;--ink:#1b2733;--mut:#5f6b78;--line:#e3e8ef;--accent:#3b6ea5;
--code-bg:#eef2f7;--code-ink:#294057;--good-bg:#e6f6ec;--good-ink:#1c7a44;--good-line:#c2e6d0;
--pill:#eef2f7;--pill-ink:#41566b;--bar:#3b6ea5;--bar-bg:#e3e8ef}
@media (prefers-color-scheme:dark){:root{--bg:#0d1117;--panel:#161b22;--ink:#e6edf3;--mut:#8b949e;
--line:#2a313a;--accent:#6cb0ef;--code-bg:#1c2430;--code-ink:#cdd9e5;--good-bg:#12261a;
--good-ink:#4cc76e;--good-line:#20492f;--pill:#20272f;--pill-ink:#adbac7;--bar:#4f8fd6;--bar-bg:#262d36}}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--ink);
font:15px/1.55 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
.wrap{max-width:1080px;margin:0 auto;padding:28px 20px 80px}
h1{font-size:26px;margin:0 0 6px}h2{font-size:20px;margin:0 0 14px;padding-bottom:8px;
border-bottom:1px solid var(--line)}h3{font-size:15px;margin:22px 0 8px;color:var(--mut)}
.sub{color:var(--mut);margin:0 0 20px;max-width:70ch}
p{max-width:78ch}code{font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
background:var(--code-bg);color:var(--code-ink);padding:1px 5px;border-radius:5px;
white-space:pre-wrap;overflow-wrap:anywhere}
section{background:var(--panel);border:1px solid var(--line);border-radius:12px;
padding:20px 22px;margin:16px 0}
.kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-top:18px}
.kpi{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:14px 16px}
.kv{font-size:26px;font-weight:700;letter-spacing:-.02em}
.kl{font-size:13px;margin-top:2px}.ks{font-size:12px;color:var(--mut);margin-top:2px}
.toc{position:sticky;top:0;z-index:5;display:flex;flex-wrap:wrap;gap:6px;padding:10px 0;
margin:4px 0 8px;background:var(--bg)}
.toc a{font-size:13px;text-decoration:none;color:var(--pill-ink);background:var(--pill);
padding:5px 11px;border-radius:999px;border:1px solid var(--line)}
.toc a:hover{color:var(--accent);border-color:var(--accent)}
.tw{overflow-x:auto;margin:4px 0}
table{border-collapse:collapse;width:100%;font-size:13.5px}
th,td{text-align:left;padding:8px 12px;border-bottom:1px solid var(--line);vertical-align:top}
th{position:sticky;top:0;background:var(--panel);font-size:12px;text-transform:uppercase;
letter-spacing:.04em;color:var(--mut)}
tbody tr:hover{background:color-mix(in srgb,var(--accent) 6%,transparent)}
td.num,th.num{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
td.skel{max-width:340px}td.verdict{max-width:460px;font-size:13px}
.barcol{width:120px}
.bar{display:inline-block;width:110px;height:7px;border-radius:4px;background:var(--bar-bg);vertical-align:middle}
.bar>span{display:block;height:100%;border-radius:4px;background:var(--bar)}
.pill{display:inline-block;background:var(--pill);color:var(--pill-ink);border-radius:999px;
padding:1px 9px;font-size:12px;white-space:nowrap}
.badge{display:inline-block;border-radius:6px;padding:1px 8px;font-size:12px;font-weight:600;margin-right:6px}
.badge.yes{background:var(--good-bg);color:var(--good-ink);border:1px solid var(--good-line)}
.badge.no{background:var(--pill);color:var(--mut)}
tr.prevent{box-shadow:inset 3px 0 var(--good-ink)}
tr.noprevent td{color:var(--mut)}
.callout{display:flex;align-items:center;gap:16px;background:var(--good-bg);
border:1px solid var(--good-line);border-radius:12px;padding:14px 18px;margin:14px 0}
.callout .big{font-size:34px;font-weight:800;color:var(--good-ink);line-height:1}
.chip{display:inline-block;background:var(--pill);border:1px solid var(--line);border-radius:6px;
padding:1px 8px;font-size:12px;margin:2px 3px 2px 0;color:var(--pill-ink)}
.chip b{color:var(--ink)}.techrow{margin:6px 0}.mut{color:var(--mut);font-weight:400}
"""

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--samples", type=int, default=5, help="payload examples per category")
    ap.add_argument("--seed", type=int, default=42, help="sampling seed")
    run(ap.parse_args())
