#!/usr/bin/env python3
"""Aggregate the per-organisation accumulated stats (result/repos/$ORG/@org/*.md)
into a single global version of each of the 4 md files.

Rules:
  - SUM metrics: summed across orgs.
  - MAX metrics: max across orgs.
  - DERIVED metrics (averages / rates / reduction %): recomputed from the
    aggregated SUM/MAX bases (you cannot average averages).
Output: result/global/{strings,nesting,files,language_stats}.md, plus
  language_stats_by_toothpicks.md (string languages sorted by toothpicks) and
  host_languages_by_characters.md (host languages sorted by raw characters).
"""
import os, sys

ROOT = "."
REPOS = os.path.join(ROOT, "result", "repos")
OUTDIR = os.path.join(ROOT, "result", "global")
SKIP = {""}  # no accumulated $ORG/@org folder yet; parsed separately later

NOTE = ("<!-- GLOBAL AGGREGATE: built from result/repos/$ORG/@org/*.md across {n} "
        "organisations ({orgs}). Sums and maxima accumulated directly; averages, "
        "rates and reduction percentages recomputed from aggregated totals. -->")

# ---------- helpers ----------
def fmt_g(x):
    """Mimic C++ default ostream double formatting (%g, 6 sig figs)."""
    return "{:g}".format(x + 0.0)

def fmt_int(x):
    return "{:d}".format(int(round(x)))

def div(a, b):
    return a / b if b else 0.0

def parse_num(s):
    s = s.strip()
    try:
        return float(s)
    except ValueError:
        return None

def split_row(row):
    return [c.strip() for c in row.strip().strip("|").split("|")]

def find_table(text, after=None):
    """Return the contiguous block of '|' table lines, optionally after a marker line."""
    lines = text.splitlines()
    i = 0
    if after is not None:
        while i < len(lines) and lines[i].strip() != after:
            i += 1
        i += 1
    while i < len(lines) and not lines[i].lstrip().startswith("|"):
        i += 1
    start = i
    rows = []
    while i < len(lines) and lines[i].lstrip().startswith("|"):
        rows.append(lines[i])
        i += 1
    return lines, start, rows

def path(org, f):
    return os.path.join(REPOS, org, "@org", f)

# ---------- discover orgs ----------
orgs = []
for name in sorted(os.listdir(REPOS)):
    if name in SKIP:
        continue
    if os.path.isfile(path(name, "strings.md")):
        orgs.append(name)

print("Aggregating %d orgs (skipping %s)." % (len(orgs), ", ".join(sorted(SKIP))))
note = NOTE.format(n=len(orgs), orgs=", ".join(orgs))
os.makedirs(OUTDIR, exist_ok=True)

# =========================================================================
# strings.md
# =========================================================================
S_SUM = {"Sample Size", "With Toothpicks", "Total Toothpicks", "With Non-Compliance",
         "Non-Compliance Count", "Compliant Samples", "With Toothpicks (Converted)",
         "Total Toothpicks (Converted)", "Raw-Form Holdable", "Matchertext-Only",
         "With Raw Nested Embedding",
         "Sum Of Per-Sample Raw Max Depth", "With Valid Nested Embedding",
         "Sum Of Per-Sample Valid Max Depth", "Raw Character Count",
         "In-Scope Samples", "In-Scope Toothpicks"}
S_MAX = {"Maximum Toothpicks", "Non-Compliance Max", "Maximum Toothpicks (Converted)",
         "In-Scope Maximum Toothpicks",
         "Highest Per-Sample Raw Max Depth", "Highest Per-Sample Valid Max Depth"}
S_DERIVED = {"Average Toothpicks", "Average With Toothpicks",
             "Avg Unmatched Matchers Per Sample", "Compliance Rate",
             "Average Toothpicks (Converted)", "Average With Toothpicks (Converted)",
             "Toothpick Reduction Total (%)", "Toothpick Reduction Average (%)",
             "Toothpick Reduction Maximum (%)", "Avg Per-Sample Raw Max Depth",
             "Avg Per-Sample Valid Max Depth"}

def build_simple(fname, sum_set, max_set, derived_fn, derived_names):
    """For strings.md / files.md: keep descriptions + Data header from ref, regenerate rows."""
    ref = open(path(orgs[0], fname)).read()
    lines, start, rows = find_table(ref, after="## Data")
    header = split_row(rows[0])
    ncols = len(header) - 1
    metric_order = [split_row(r)[0] for r in rows[2:]]

    agg = {m: [0.0] * ncols for m in metric_order}
    seen_max = {m: [False] * ncols for m in max_set}
    for org in orgs:
        _, _, r = find_table(open(path(org, fname)).read(), after="## Data")
        for row in r[2:]:
            cells = split_row(row)
            m = cells[0]
            if m not in agg:
                agg[m] = [0.0] * ncols
                metric_order.append(m)
            for c in range(ncols):
                v = parse_num(cells[c + 1]) if c + 1 < len(cells) else None
                if v is None:
                    continue
                if m in sum_set:
                    agg[m][c] += v
                elif m in max_set:
                    if not seen_max[m][c] or v > agg[m][c]:
                        agg[m][c] = v
                        seen_max[m][c] = True
                # derived: ignored here, recomputed below

    # recompute derived
    for m in derived_names:
        agg[m] = [derived_fn(m, c, agg) for c in range(ncols)]

    # emit
    out = []
    prefix_idx = lines.index(rows[0])
    out.append(lines[0])           # title
    out.append(note)               # provenance comment
    out.extend(lines[1:prefix_idx])  # rest of descriptions incl "## Data" + blank
    out.append(rows[0])            # data header
    out.append(rows[1])            # separator
    for m in metric_order:
        vals = agg[m]
        out.append("| " + " | ".join([m] + [fmt_g(v) for v in vals]) + " |")
    text = "\n".join(out) + "\n"
    open(os.path.join(OUTDIR, fname), "w").write(text)
    return agg, ncols

def s_derived(m, c, a):
    g = lambda k: a[k][c] if k in a else 0.0
    # Compliance, rewrite and nesting stats cover only in-scope samples (all
    # samples except BinaryData and PseudoBinaryData string literals). Results
    # from before the split have no in-scope rows: everything was in scope.
    scoped = "In-Scope Samples" in a
    n = g("In-Scope Samples") if scoped else g("Sample Size")
    tp = g("In-Scope Toothpicks") if scoped else g("Total Toothpicks")
    tp_max = g("In-Scope Maximum Toothpicks") if scoped else g("Maximum Toothpicks")
    if m == "Average Toothpicks":            return div(g("Total Toothpicks"), g("Sample Size"))
    if m == "Average With Toothpicks":       return div(g("Total Toothpicks"), g("With Toothpicks"))
    if m == "Avg Unmatched Matchers Per Sample": return div(g("Non-Compliance Count"), n)
    if m == "Compliance Rate":               return div(g("Compliant Samples"), n) * 100
    if m == "Average Toothpicks (Converted)":     return div(g("Total Toothpicks (Converted)"), n)
    if m == "Average With Toothpicks (Converted)": return div(g("Total Toothpicks (Converted)"), g("With Toothpicks (Converted)"))
    if m == "Toothpick Reduction Total (%)":
        return div(tp - g("Total Toothpicks (Converted)"), tp) * 100
    if m == "Toothpick Reduction Average (%)":
        avg = div(tp, n)
        avgc = div(g("Total Toothpicks (Converted)"), n)
        return div(avg - avgc, avg) * 100
    if m == "Toothpick Reduction Maximum (%)":
        return div(tp_max - g("Maximum Toothpicks (Converted)"), tp_max) * 100
    if m == "Avg Per-Sample Raw Max Depth":  return div(g("Sum Of Per-Sample Raw Max Depth"), n)
    if m == "Avg Per-Sample Valid Max Depth": return div(g("Sum Of Per-Sample Valid Max Depth"), n)
    return 0.0

s_agg, s_ncols = build_simple("strings.md", S_SUM, S_MAX, s_derived, S_DERIVED)

# =========================================================================
# files.md
# =========================================================================
F_SUM = {"Sample Size", "With Violation", "Total Violations",
         "With Violation Relaxed", "Total Relaxed Violations",
         "Skipped Oversized", "Skipped Binary", "Skipped Timeout"}
F_MAX = {"Maximum Violations", "Maximum Relaxed Violations"}
F_DERIVED = {"Average Violations", "Compliance Rate",
             "Average Relaxed Violations", "Compliance Rate Relaxed"}

def f_derived(m, c, a):
    g = lambda k: a[k][c] if k in a else 0.0
    if m == "Average Violations":         return div(g("Total Violations"), g("Sample Size"))
    if m == "Compliance Rate":            return div(g("Sample Size") - g("With Violation"), g("Sample Size")) * 100
    if m == "Average Relaxed Violations": return div(g("Total Relaxed Violations"), g("Sample Size"))
    if m == "Compliance Rate Relaxed":    return div(g("Sample Size") - g("With Violation Relaxed"), g("Sample Size")) * 100
    return 0.0

f_agg, _ = build_simple("files.md", F_SUM, F_MAX, f_derived, F_DERIVED)

# =========================================================================
# nesting.md  (sum every column per level; union of levels)
# =========================================================================
ref = open(path(orgs[0], "nesting.md")).read()
nlines, _, nrows = find_table(ref)
nhdr = split_row(nrows[0])          # Level | Strings Raw | ... (6 value cols)
ncols = len(nhdr) - 1
levels = {}                         # level(int) -> [6 sums]
for org in orgs:
    _, _, r = find_table(open(path(org, "nesting.md")).read())
    for row in r[2:]:
        cells = split_row(row)
        lvl = int(float(cells[0]))
        acc = levels.setdefault(lvl, [0.0] * ncols)
        for c in range(ncols):
            v = parse_num(cells[c + 1]) if c + 1 < len(cells) else None
            if v is not None:
                acc[c] += v
out = [nlines[0], note, "", nrows[0], nrows[1]]
for lvl in sorted(levels):
    out.append("| " + " | ".join([str(lvl)] + [fmt_int(v) for v in levels[lvl]]) + " |")
open(os.path.join(OUTDIR, "nesting.md"), "w").write("\n".join(out) + "\n")

# =========================================================================
# language_stats.md  (sum Count/Violations/Toothpicks per language; recompute %)
# =========================================================================
ref = open(path(orgs[0], "language_stats.md")).read()
llines, _, lrows = find_table(ref)
lhdr = split_row(lrows[0])          # String Language | Count | % | Violations | Toothpicks
langs = {}                          # name -> [count, violations, toothpicks]
for org in orgs:
    _, _, r = find_table(open(path(org, "language_stats.md")).read())
    for row in r[2:]:
        cells = split_row(row)
        name = cells[0]
        cnt = parse_num(cells[1]) or 0.0
        vio = parse_num(cells[3]) or 0.0
        tp = parse_num(cells[4]) or 0.0
        acc = langs.setdefault(name, [0.0, 0.0, 0.0])
        acc[0] += cnt; acc[1] += vio; acc[2] += tp
total = sum(v[0] for v in langs.values())
out = [llines[0], note, "", lrows[0], lrows[1]]
for name in sorted(langs, key=lambda k: (-langs[k][0], k)):
    cnt, vio, tp = langs[name]
    pct = div(cnt, total) * 100
    out.append("| %s | %s | %.2f%% | %s | %s |" % (name, fmt_int(cnt), pct, fmt_int(vio), fmt_int(tp)))
open(os.path.join(OUTDIR, "language_stats.md"), "w").write("\n".join(out) + "\n")

# language_stats_by_toothpicks.md  (the same string languages, sorted by toothpicks)
tp_total = sum(v[2] for v in langs.values())
out = ["# String Language Distribution by Toothpicks", note, "",
       "| String Language | Toothpicks | % of Toothpicks | Count | % of Strings | Violations |",
       "|---|---|---|---|---|---|"]
for name in sorted(langs, key=lambda k: (-langs[k][2], k)):
    cnt, vio, tp = langs[name]
    out.append("| %s | %s | %.2f%% | %s | %.2f%% | %s |" % (
        name, fmt_int(tp), div(tp, tp_total) * 100, fmt_int(cnt), div(cnt, total) * 100, fmt_int(vio)))
open(os.path.join(OUTDIR, "language_stats_by_toothpicks.md"), "w").write("\n".join(out) + "\n")

# =========================================================================
# host_languages_by_characters.md  (string stats per host language, summed from
# result/repos/$ORG/@org/language_stats/<Language>/strings.md)
# =========================================================================
H_SUM = {"Raw Character Count": "chars", "Sample Size": "n", "Total Toothpicks": "tp",
         "In-Scope Samples": "scope_n", "In-Scope Toothpicks": "scope_tp",
         "Total Toothpicks (Converted)": "tpc", "Non-Compliance Count": "viol",
         "Compliant Samples": "ok"}
hosts = {}                          # host language -> {field: sum}
for org in orgs:
    base = path(org, "language_stats")
    if not os.path.isdir(base):
        continue
    for lang in os.listdir(base):
        p = os.path.join(base, lang, "strings.md")
        if not os.path.isfile(p):
            continue
        acc = hosts.setdefault(lang, dict.fromkeys(H_SUM.values(), 0.0))
        _, _, r = find_table(open(p).read(), after="## Data")
        row = {split_row(x)[0]: parse_num(split_row(x)[1]) or 0.0 for x in r[2:]}
        row.setdefault("In-Scope Samples", row.get("Sample Size", 0.0))
        row.setdefault("In-Scope Toothpicks", row.get("Total Toothpicks", 0.0))
        for metric, key in H_SUM.items():
            acc[key] += row.get(metric, 0.0)
tot = {k: sum(h[k] for h in hosts.values()) for k in H_SUM.values()}
out = ["# String Statistics per Host Language by Characters", note,
       "<!-- Characters, strings and toothpicks count every string literal; reduction "
       "and compliance cover only in-scope ones (not BinaryData or PseudoBinaryData). "
       "Per-org character counts are stored with 6 significant digits, so the last "
       "digits of the sums are approximate. -->", "",
       "| Host Language | Characters | % of Characters | Strings | % of Strings | Toothpicks "
       "| % of Toothpicks | Reduction | Violations | Compliance |",
       "|---|---|---|---|---|---|---|---|---|---|"]
def host_row(name, h):
    return "| %s | %s | %.2f%% | %s | %.2f%% | %s | %.2f%% | %.2f%% | %s | %.2f%% |" % (
        name, fmt_int(h["chars"]), div(h["chars"], tot["chars"]) * 100,
        fmt_int(h["n"]), div(h["n"], tot["n"]) * 100,
        fmt_int(h["tp"]), div(h["tp"], tot["tp"]) * 100,
        div(h["scope_tp"] - h["tpc"], h["scope_tp"]) * 100,
        fmt_int(h["viol"]), div(h["ok"], h["scope_n"]) * 100)
for name in sorted(hosts, key=lambda k: (-hosts[k]["chars"], k)):
    if hosts[name]["n"]:            # hosts with comments but no strings are left out
        out.append(host_row(name, hosts[name]))
out.append(host_row("**Total**", tot))
open(os.path.join(OUTDIR, "host_languages_by_characters.md"), "w").write("\n".join(out) + "\n")

# ---------- console summary ----------
print("\nWrote 6 files to %s\n" % OUTDIR)
print("Key global figures (Strings column):")
def gv(a, m): return a[m][0]
print("  Sample Size            : %s" % fmt_g(gv(s_agg, "Sample Size")))
print("  Total Toothpicks       : %s" % fmt_g(gv(s_agg, "Total Toothpicks")))
print("  Compliance Rate        : %s %%" % fmt_g(gv(s_agg, "Compliance Rate")))
print("  Toothpick Reduction Tot: %s %%" % fmt_g(gv(s_agg, "Toothpick Reduction Total (%)")))
print("Files:")
print("  Sample Size (files)    : %s" % fmt_g(f_agg["Sample Size"][0]))
print("  Compliance Rate        : %s %%" % fmt_g(f_agg["Compliance Rate"][0]))
print("Distinct language categories : %d" % len(langs))
print("Total classified strings     : %s" % fmt_int(total))
print("Nesting levels               : %d (max level %d)" % (len(levels), max(levels)))
