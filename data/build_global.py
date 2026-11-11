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
  host_languages_by_characters.md (host languages sorted by raw characters), and
  raw_forms.md (how each host's native raw string forms hold its strings),
  whole_file.md (files compliant fragment by fragment vs. as a whole),
  toothpick_survivors.md (why the toothpicks left after the rewrite survive),
  duplicates.md (exact content duplicates across the corpus), and
  deduplicated.md (the main figures with each distinct file content counted once).
"""
import os, re, sys

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
    """Cells of a table row; a pipe escaped as \\| stays inside its cell."""
    return [c.strip() for c in re.split(r"(?<!\\)\|", row.strip()[1:-1])]

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
KEPT = ("Kept Toothpicks: Non-Compliant", "Kept Toothpicks: Not Matchertext Decoded",
        "Kept Toothpicks: Already Raw", "Kept Toothpicks: Unknown Escape",
        "Kept Toothpicks: Interpolation", "Kept Toothpicks: Escaped Backslash", "Kept Toothpicks: Not Writable Raw",
        "Kept Toothpicks: Control Character")
S_SUM |= set(KEPT)
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
         "Skipped Oversized", "Skipped Binary", "Skipped Timeout",
         "Whole-File Compliant", "Whole-File Compliant Relaxed",
         "Fragments And Whole File Compliant", "Fragments Compliant Only",
         "Whole File Compliant Only", "Neither Compliant",
         "Fragments And Whole File Compliant Relaxed", "Fragments Compliant Only Relaxed",
         "Whole File Compliant Only Relaxed", "Neither Compliant Relaxed",
         "Fragment-Free Files", "Fragment-Free Whole-File Compliant",
         "Fragment-Free Whole-File Compliant Relaxed"}
F_MAX = {"Maximum Violations", "Maximum Relaxed Violations"}
F_DERIVED = {"Average Violations", "Compliance Rate",
             "Average Relaxed Violations", "Compliance Rate Relaxed",
             "Whole-File Compliance Rate", "Whole-File Compliance Rate Relaxed"}

def f_derived(m, c, a):
    g = lambda k: a[k][c] if k in a else 0.0
    if m == "Average Violations":         return div(g("Total Violations"), g("Sample Size"))
    if m == "Compliance Rate":            return div(g("Sample Size") - g("With Violation"), g("Sample Size")) * 100
    if m == "Average Relaxed Violations": return div(g("Total Relaxed Violations"), g("Sample Size"))
    if m == "Compliance Rate Relaxed":    return div(g("Sample Size") - g("With Violation Relaxed"), g("Sample Size")) * 100
    if m == "Whole-File Compliance Rate": return div(g("Whole-File Compliant"), g("Sample Size")) * 100
    if m == "Whole-File Compliance Rate Relaxed":
        return div(g("Whole-File Compliant Relaxed"), g("Sample Size")) * 100
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
# language_stats.md  (sum Count/Violations/Toothpicks/Converted per language;
# recompute % and the reduction)
# =========================================================================
OUT_OF_SCOPE = {"BinaryData", "PseudoBinaryData"}   # not rewritten: no reduction
ref = open(path(orgs[0], "language_stats.md")).read()
llines, _, lrows = find_table(ref)
langs = {}                          # name -> [count, violations, toothpicks, converted]
for org in orgs:
    _, _, r = find_table(open(path(org, "language_stats.md")).read())
    for row in r[2:]:
        cells = split_row(row)
        name = cells[0]
        cnt = parse_num(cells[1]) or 0.0
        vio = parse_num(cells[3]) or 0.0
        tp = parse_num(cells[4]) or 0.0
        # results from before the per-category rewrite have no converted column
        conv = parse_num(cells[5]) if len(cells) > 5 and parse_num(cells[5]) is not None else tp
        acc = langs.setdefault(name, [0.0, 0.0, 0.0, 0.0])
        acc[0] += cnt; acc[1] += vio; acc[2] += tp; acc[3] += conv
total = sum(v[0] for v in langs.values())
def reduction(name, tp, conv):
    return "-" if name in OUT_OF_SCOPE or not tp else "%.2f%%" % (div(tp - conv, tp) * 100)
out = [llines[0], note, "",
       "| String Language | Count | % | Violations | Toothpicks | Converted Toothpicks | Reduction |",
       "|---|---|---|---|---|---|---|"]
for name in sorted(langs, key=lambda k: (-langs[k][0], k)):
    cnt, vio, tp, conv = langs[name]
    out.append("| %s | %s | %.2f%% | %s | %s | %s | %s |" % (
        name, fmt_int(cnt), div(cnt, total) * 100, fmt_int(vio), fmt_int(tp), fmt_int(conv),
        reduction(name, tp, conv)))
open(os.path.join(OUTDIR, "language_stats.md"), "w").write("\n".join(out) + "\n")

# language_stats_by_toothpicks.md  (the same string languages, sorted by toothpicks)
tp_total = sum(v[2] for v in langs.values())
conv_total = sum(v[3] for k, v in langs.items() if k not in OUT_OF_SCOPE)
out = ["# String Language Distribution by Toothpicks", note, "",
       "| String Language | Toothpicks | % of Toothpicks | Converted Toothpicks | % of Converted "
       "| Reduction | Count | % of Strings | Violations |",
       "|---|---|---|---|---|---|---|---|---|"]
for name in sorted(langs, key=lambda k: (-langs[k][2], k)):
    cnt, vio, tp, conv = langs[name]
    conv_share = "-" if name in OUT_OF_SCOPE else "%.2f%%" % (div(conv, conv_total) * 100)
    out.append("| %s | %s | %.2f%% | %s | %s | %s | %s | %.2f%% | %s |" % (
        name, fmt_int(tp), div(tp, tp_total) * 100, fmt_int(conv), conv_share,
        reduction(name, tp, conv), fmt_int(cnt), div(cnt, total) * 100, fmt_int(vio)))
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

# =========================================================================
# toothpick_survivors.md  (why the toothpicks left after the rewrite survive, per
# host language, from @org/language_stats/<Language>/strings.md)
# =========================================================================
CAUSE_LABELS = ("Non-Compliant", "Not Matchertext Decoded", "Already Raw", "Unknown Escape",
                "Interpolation", "Escaped Backslash", "Not Writable Raw", "Control Character")
survivors = {}                      # host -> [converted, causes...]
for org in orgs:
    base = path(org, "language_stats")
    if not os.path.isdir(base):
        continue
    for lang in os.listdir(base):
        p = os.path.join(base, lang, "strings.md")
        if not os.path.isfile(p):
            continue
        _, _, r = find_table(open(p).read(), after="## Data")
        row = {split_row(x)[0]: parse_num(split_row(x)[1]) or 0.0 for x in r[2:]}
        if KEPT[0] not in row:
            continue                # results from before the per-cause breakdown
        acc = survivors.setdefault(lang, [0.0] * (1 + len(KEPT)))
        acc[0] += row.get("Total Toothpicks (Converted)", 0.0)
        for k, metric in enumerate(KEPT):
            acc[1 + k] += row.get(metric, 0.0)
survivors = {h: v for h, v in survivors.items() if v[0]}
surv_total = [sum(v[k] for v in survivors.values()) for k in range(1 + len(KEPT))]
out = ["# Toothpicks Left After the Rewrite, by Cause", note,
       "<!-- In-scope string literals only. Non-Compliant literals are not rewritten and keep all "
       "their toothpicks, as do compliant literals whose decoded content is not matchertext "
       "(\\x5B is [). In a rewritten literal, a toothpick survives as a literal backslash of an "
       "already raw literal, an unknown escape kept as written (PHP, OCaml), a backslash opening an "
       "interpolation (Swift \\\\(name)), an escape decoding to "
       "a backslash, an escape a raw literal cannot write (a byte that is not UTF-8, a lone "
       "surrogate), or a control character other than tab, LF, and CR, kept verbatim. Each "
       "row's causes add up to its converted toothpicks. -->", "",
       "| Host Language | Converted Toothpicks | " + " | ".join(CAUSE_LABELS) + " |",
       "|---|---|" + "---|" * len(CAUSE_LABELS)]
def survivor_row(name, v):
    return "| %s | %s | " % (name, fmt_int(v[0])) + " | ".join(
        "%s (%.1f%%)" % (fmt_int(c), div(c, v[0]) * 100) for c in v[1:]) + " |"
for h in sorted(survivors, key=lambda k: (-survivors[k][0], k)):
    out.append(survivor_row(h, survivors[h]))
if survivors:
    out.append(survivor_row("**Total**", surv_total))
open(os.path.join(OUTDIR, "toothpick_survivors.md"), "w").write("\n".join(out) + "\n")

# ---------- console summary ----------
# =========================================================================
# raw_forms.md  (sum result/repos/$ORG/@org/language_stats/<Language>/raw_forms.md:
# every string of every host language against the raw forms of every language)
# =========================================================================
POPS = ("All Strings", "In-Scope Strings", "Matchertext Strings")
raw = {}                            # host -> {(language, form, measure): [all, in-scope, matchertext]}
for org in orgs:
    base = path(org, "language_stats")
    if not os.path.isdir(base):
        continue
    for lang in os.listdir(base):
        p = os.path.join(base, lang, "raw_forms.md")
        if not os.path.isfile(p):
            continue
        rows = raw.setdefault(lang, {})
        _, _, r = find_table(open(p).read())
        for row in r[2:]:
            cells = split_row(row)
            acc = rows.setdefault(tuple(cells[:3]), [0.0] * len(POPS))
            for c in range(len(POPS)):
                acc[c] += parse_num(cells[3 + c]) or 0.0
STRINGS, CONTROL = ("-", "-", "String literals"), ("-", "-", "Contain control characters")
OWN, ANY = ("Own language", "Any form", "Holds"), ("Any language", "Any form", "Holds")
raw = {h: rows for h, rows in raw.items() if rows.get(STRINGS, [0])[0]}
raw_total = {}
for rows in raw.values():
    for key, v in rows.items():
        acc = raw_total.setdefault(key, [0.0] * len(POPS))
        for c in range(len(POPS)):
            acc[c] += v[c]
forms = [k for k in raw_total if k[0] not in ("-", "Own language", "Any language")]
get = lambda rows, key, c: rows.get(key, [0.0] * len(POPS))[c]
pct_of = lambda rows, key, c: div(get(rows, key, c), get(rows, STRINGS, c)) * 100

out = ["# Native Raw String Forms", note,
       "<!-- Every string literal of every host language checked against the native raw "
       "string forms of every language. All counts every string literal; In-Scope leaves out "
       "binary data (BinaryData, PseudoBinaryData); Matchertext keeps the in-scope literals "
       "whose decoded content a matchertext literal holds. A form holds a content it can "
       "write verbatim; control characters other than tab, LF and CR are written verbatim. "
       "C++ counts only R\"(...)\"; a custom delimiter R\"d(...)d\" can avoid any )\". -->", "",
       "## Per Host Language", "",
       "| Host Language | In-Scope Strings | Own Raw Form | Any Language's Raw Form "
       "| Matchertext Strings | Own Raw Form | Any Language's Raw Form "
       "| Control Characters (In-Scope) | Control Characters (All) |",
       "|---|---|---|---|---|---|---|---|---|"]
def summary_row(name, rows):
    return "| %s | %s | %.2f%% | %.2f%% | %s | %.2f%% | %.2f%% | %.2f%% | %.2f%% |" % (
        name, fmt_int(get(rows, STRINGS, 1)), pct_of(rows, OWN, 1), pct_of(rows, ANY, 1),
        fmt_int(get(rows, STRINGS, 2)), pct_of(rows, OWN, 2), pct_of(rows, ANY, 2),
        pct_of(rows, CONTROL, 1), pct_of(rows, CONTROL, 0))
hosts_order = sorted(raw, key=lambda h: (-get(raw[h], STRINGS, 1), h))
for h in hosts_order:
    out.append(summary_row(h, raw[h]))
out.append(summary_row("**Total**", raw_total))

out += ["", "## Per Raw Form, All Host Languages", "",
        "| Language | Raw Form | Measure | In-Scope Strings | % of In-Scope "
        "| Matchertext Strings | % of Matchertext |",
        "|---|---|---|---|---|---|---|"]
for key in forms:
    out.append("| %s | %s | %s | %s | %.2f%% | %s | %.2f%% |" % (
        key + (fmt_int(get(raw_total, key, 1)), pct_of(raw_total, key, 1),
               fmt_int(get(raw_total, key, 2)), pct_of(raw_total, key, 2))))

holds = [k for k in forms if k[2] == "Holds"]
out += ["", "## Held per Host Language and Raw Form", "",
        "<!-- Share of each host language's in-scope strings (and, after the slash, of its "
        "matchertext strings) that each raw form holds. -->", "",
        "| Host Language | " + " | ".join("%s %s" % (k[0], k[1]) for k in holds) + " |",
        "|---|" + "---|" * len(holds)]
for h in hosts_order:
    out.append("| %s | " % h + " | ".join(
        "%.1f%% / %.1f%%" % (pct_of(raw[h], k, 1), pct_of(raw[h], k, 2)) for k in holds) + " |")
open(os.path.join(OUTDIR, "raw_forms.md"), "w").write("\n".join(out) + "\n")

# =========================================================================
# whole_file.md  (per host language: files compliant fragment by fragment vs.
# as a whole, from @org/language_stats/<Language>/{files,file_examples}.md)
# =========================================================================
CELLS = ("Fragments And Whole File Compliant", "Fragments Compliant Only",
         "Whole File Compliant Only", "Neither Compliant")
RELAXED_CELLS = tuple(c + " Relaxed" for c in CELLS)
FREE = ("Fragment-Free Files", "Fragment-Free Whole-File Compliant",
        "Fragment-Free Whole-File Compliant Relaxed")
EXAMPLES_PER_CELL = 10
whole = {}                          # host -> {metric: sum}
examples = {}                       # host -> (fragments only, whole only) example paths
for org in orgs:
    base = path(org, "language_stats")
    if not os.path.isdir(base):
        continue
    for lang in os.listdir(base):
        p = os.path.join(base, lang, "files.md")
        if not os.path.isfile(p):
            continue
        _, _, r = find_table(open(p).read(), after="## Data")
        row = {split_row(x)[0]: parse_num(split_row(x)[1]) or 0.0 for x in r[2:]}
        if "Whole-File Compliant" not in row:
            continue                # results from before the whole-file check
        acc = whole.setdefault(lang, {})
        for metric in ("Sample Size", "Whole-File Compliant", "Whole-File Compliant Relaxed") + CELLS + RELAXED_CELLS + FREE:
            acc[metric] = acc.get(metric, 0.0) + row.get(metric, 0.0)
        ex = examples.setdefault(lang, ([], []))
        ep = os.path.join(base, lang, "file_examples.md")
        if os.path.isfile(ep):
            cell = None
            for line in open(ep):
                if line.startswith("## "):
                    cell = 0 if "Fragments" in line else 1
                elif line.startswith("- ") and cell is not None and len(ex[cell]) < EXAMPLES_PER_CELL:
                    ex[cell].append("%s/%s" % (org, line[2:].strip()))
whole = {h: v for h, v in whole.items() if v["Sample Size"]}
whole_total = {m: sum(v[m] for v in whole.values()) for m in next(iter(whole.values()), {})}
whole_hosts = sorted(whole, key=lambda k: (-whole[k]["Sample Size"], k))
out = ["# Whole-File Compliance", note,
       "<!-- Fragments: all string literals and comments of the file are compliant; this is the "
       "file compliance of files.md. Whole file: the file's entire source text is matchertext, "
       "string literals of binary data (out of matchertext's scope, like in the fragments) blanked "
       "out. Character literals are not fragments, so they count only in the whole-file check. "
       "Fragment-free files have no string literal and no comment, so their fragments are "
       "compliant by default. -->"]

def matrix(title, comment, cells, wholes, drop_free):
    """One 2x2 table per host. With drop_free, fragment-free files (all in the first two
    cells, by whether the whole file is compliant) are left out."""
    rows = ["", "## " + title, "", comment, "",
            "| Host Language | Files | Both | Fragments Only | Whole File Only | Neither "
            "| Fragment-Free Files | Whole-File Compliance |",
            "|---|---|---|---|---|---|---|---|"]
    def row(name, v):
        both, frag, wonly, neither = (v[c] for c in cells)
        free, free_whole = v[FREE[0]], v[wholes[1]]
        n, w = v["Sample Size"], v[wholes[0]]
        if drop_free:
            both, frag, n, w = both - free_whole, frag - (free - free_whole), n - free, w - free_whole
        return "| %s | %s | %s | %s | %s | %s | %s | %.2f%% |" % (
            name, fmt_int(n), fmt_int(both), fmt_int(frag), fmt_int(wonly), fmt_int(neither),
            "-" if drop_free else fmt_int(free), div(w, n) * 100)
    rows += [row(h, whole[h]) for h in whole_hosts]
    if whole:
        rows.append(row("**Total**", whole_total))
    return rows

out += matrix("Strict", "<!-- Comments and the whole file under the strict rule. -->",
              CELLS, ("Whole-File Compliant", FREE[1]), False)
out += matrix("Relaxed", "<!-- Comments and the whole file under the relaxed rule; string literals are strict either way. -->",
              RELAXED_CELLS, ("Whole-File Compliant Relaxed", FREE[2]), False)
out += matrix("Strict, Without Fragment-Free Files",
              "<!-- The strict matrix over the files with at least one string literal or comment. -->",
              CELLS, ("Whole-File Compliant", FREE[1]), True)
for title, cell in (("Fragments Compliant, Whole File Not", 0), ("Whole File Compliant, Fragments Not", 1)):
    out += ["", "## Examples: %s" % title, ""]
    for h in sorted(examples):
        for e in examples[h][cell]:
            out.append("- %s: %s" % (h, e))
open(os.path.join(OUTDIR, "whole_file.md"), "w").write("\n".join(out) + "\n")

# =========================================================================
# duplicates.md and deduplicated.md  (merge result/repos/$ORG/@org/file_contents.tsv:
# one line per distinct file content, its hash, how many files have it, their
# string samples, and the stats of one copy)
# =========================================================================
# Stats of one copy, in file_contents.tsv order after the hash, files, and strings.
COPY = ("strings", "scope", "compliant", "tp", "scope_tp", "conv") + tuple("kept%d" % k for k in range(len(KEPT))) + \
       ("docs", "docs_ok", "docs_ok_relaxed", "fragments", "fragments_relaxed", "whole", "whole_relaxed")
contents = {}                       # hash -> files * 2**32 + string samples
dedup = dict.fromkeys(COPY, 0.0)    # each distinct content once
alld = dict.fromkeys(COPY, 0.0)     # every copy
cells = {"distinct": [0.0] * 4, "all": [0.0] * 4}
cats_dedup, cats_all = {}, {}       # category -> [count, violations, toothpicks, converted]
have_copies = False
for org in orgs:
    p = path(org, "file_contents.tsv")
    if not os.path.isfile(p):
        continue
    with open(p) as f:
        for line in f:
            if line.startswith("#"):
                continue
            cols = line.rstrip("\n").split("\t")
            key = int(cols[0], 16)
            files, strings = int(cols[1]), int(cols[2])
            first = key not in contents
            contents[key] = contents.get(key, 0) + (files << 32) + strings
            if len(cols) < 4 + len(COPY):
                continue            # results from before the per-content stats
            have_copies = True
            copy = dict(zip(COPY, map(float, cols[3:3 + len(COPY)])))
            cell = 2 * (copy["fragments"] == 0) + (copy["whole"] == 0)   # both, fragments only, whole only, neither
            parsed = [c.split(":") for c in cols[3 + len(COPY)].split(",") if c]
            for k in COPY:
                alld[k] += copy[k] * files
            cells["all"][cell] += files
            for name, *nums in parsed:
                acc = cats_all.setdefault(name, [0.0] * 4)
                for n in range(4):
                    acc[n] += float(nums[n]) * files
            if first:               # the first copy seen stands for the content
                for k in COPY:
                    dedup[k] += copy[k]
                cells["distinct"][cell] += 1
                for name, *nums in parsed:
                    acc = cats_dedup.setdefault(name, [0.0] * 4)
                    for n in range(4):
                        acc[n] += float(nums[n])
files_total = strings_total = dup_files = dup_strings = 0
for packed in contents.values():
    files, strings = packed >> 32, packed & 0xFFFFFFFF
    files_total += files
    strings_total += strings
    if files > 1:                   # every copy but one is a duplicate
        dup_files += files - 1
        dup_strings += strings - strings // files
out = ["# Exact Content Duplicates", note,
       "<!-- Parsed files grouped by a 64-bit FNV-1a hash of their content across all "
       "organizations. Every copy of a content but one counts as a duplicate. -->", "",
       "| Metric | Value |", "|---|---|",
       "| Parsed Files | %s |" % fmt_int(files_total),
       "| Distinct Contents | %s |" % fmt_int(len(contents)),
       "| Duplicate Files | %s |" % fmt_int(dup_files),
       "| Duplicate File Share (%%) | %.2f |" % (div(dup_files, files_total) * 100),
       "| String Samples | %s |" % fmt_int(strings_total),
       "| String Samples in Duplicate Files | %s |" % fmt_int(dup_strings),
       "| Duplicate String Sample Share (%%) | %.2f |" % (div(dup_strings, strings_total) * 100)]
open(os.path.join(OUTDIR, "duplicates.md"), "w").write("\n".join(out) + "\n")

if have_copies:
    n_distinct = sum(cells["distinct"])
    def both(label, fn, fmt):
        a, d = fn(alld, files_total), fn(dedup, n_distinct)
        return "| %s | %s | %s |" % (label, fmt(a), fmt(d))
    pct = lambda x: "%.2f%%" % x
    out = ["# Deduplicated Figures", note,
           "<!-- The main figures over every parsed file, and with each distinct file content "
           "(64-bit FNV-1a hash) counted once, from the stats of its first copy. All Files counts every "
           "copy with the stats of the first, so it can differ slightly from strings.md when one "
           "content is parsed as two languages (a .h file as C and as C++). File compliance: "
           "all string literals and comments compliant; whole file: the entire source text, binary "
           "data literals blanked out, is matchertext. -->", "",
           "| Metric | All Files | Distinct Contents |", "|---|---|---|",
           both("Files", lambda a, n: n, fmt_int),
           both("String Samples", lambda a, n: a["strings"], fmt_int),
           both("In-Scope String Samples", lambda a, n: a["scope"], fmt_int),
           both("String Compliance", lambda a, n: div(a["compliant"], a["scope"]) * 100, pct),
           both("Total Toothpicks", lambda a, n: a["tp"], fmt_int),
           both("In-Scope Toothpicks", lambda a, n: a["scope_tp"], fmt_int),
           both("Converted Toothpicks", lambda a, n: a["conv"], fmt_int),
           both("Toothpick Reduction", lambda a, n: div(a["scope_tp"] - a["conv"], a["scope_tp"]) * 100, pct)]
    for k, metric in enumerate(KEPT):
        out.append(both(metric + " (% of converted)",
                        lambda a, n, k=k: div(a["kept%d" % k], a["conv"]) * 100, pct))
    out += [both("Comments", lambda a, n: a["docs"], fmt_int),
            both("Comment Compliance", lambda a, n: div(a["docs_ok"], a["docs"]) * 100, pct),
            both("Comment Compliance Relaxed", lambda a, n: div(a["docs_ok_relaxed"], a["docs"]) * 100, pct),
            both("File Compliance", lambda a, n: div(a["fragments"], n) * 100, pct),
            both("File Compliance Relaxed", lambda a, n: div(a["fragments_relaxed"], n) * 100, pct),
            both("Whole-File Compliance", lambda a, n: div(a["whole"], n) * 100, pct),
            both("Whole-File Compliance Relaxed", lambda a, n: div(a["whole_relaxed"], n) * 100, pct)]
    for c, label in enumerate(("Both", "Fragments Only", "Whole File Only", "Neither")):
        out.append("| %s (files) | %s | %s |" % (label, fmt_int(cells["all"][c]), fmt_int(cells["distinct"][c])))
    out += ["", "## String Categories, Each Distinct Content Once", "",
            "| String Language | Count | % | Violations | Toothpicks | Converted Toothpicks | Reduction |",
            "|---|---|---|---|---|---|---|"]
    cat_total = sum(v[0] for v in cats_dedup.values())
    for name in sorted(cats_dedup, key=lambda k: (-cats_dedup[k][0], k)):
        cnt, vio, tp, conv = cats_dedup[name]
        out.append("| %s | %s | %.2f%% | %s | %s | %s | %s |" % (
            name, fmt_int(cnt), div(cnt, cat_total) * 100, fmt_int(vio), fmt_int(tp), fmt_int(conv),
            reduction(name, tp, conv)))
    open(os.path.join(OUTDIR, "deduplicated.md"), "w").write("\n".join(out) + "\n")
del contents

print("\nWrote %d files to %s\n" % (11 if have_copies else 10, OUTDIR))
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
