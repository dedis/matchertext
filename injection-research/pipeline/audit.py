"""Label audit: blinded rater sheets for the classification audit sample, and
the read-back that turns graded labels into agreement, per-layer precision, and
a label-adjusted matcher-delimitable ceiling.

    python3 pipeline/audit.py sheets   freeze the sample, write the two rater sheets
    python3 pipeline/audit.py gold     prefill gold.csv where the raters agree
    python3 pipeline/audit.py report   print the metrics

latex.py quotes the metrics once gold.csv is complete. The labeling guide is
audit/README.md.
"""
import argparse
import csv
import hashlib
import math
import sqlite3
import sys
from collections import Counter, defaultdict
from pathlib import Path

import export
import matchertext
import taxonomy

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
AUDIT = ROOT / "audit"
SAMPLE = AUDIT / "sample.csv"
RATERS = (AUDIT / "rater_a.csv", AUDIT / "rater_b.csv")
GOLD = AUDIT / "gold.csv"

SHEET = ["cve_id", "description", "injection", "syntax", "notes"]
SYNTAX_VALUES = (set(taxonomy.SYNTAXES) - {"unknown"}) | {"other", "unclear"}
LAYERS = ("cwe", "rule", "nb", "unknown")
NONE = "none"  # the category of a CVE graded as not injection


def write_csv(path, header, rows):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)
    print(f"wrote {path.relative_to(ROOT)}")


def sheets(con):
    """Freeze the sample and write one blinded sheet per rater.

    The sheets carry no pipeline label and are ordered by a salted hash of the
    CVE id. The salt matters: the sample itself takes the lowest unsalted hashes
    per layer, so that order would group rows by layer.
    """
    for path in (SAMPLE, *RATERS):
        if path.exists():
            sys.exit(f"{path.relative_to(ROOT)} exists; delete it to draw a new sample")
    AUDIT.mkdir(exist_ok=True)
    rows = export.audit_rows(con)
    write_csv(SAMPLE, ["cve_id", "method", "syntax_type"],
              sorted((cid, method, syn) for cid, _, syn, method, _, _ in rows))
    blind = sorted(((cid, desc or "", "", "", "") for cid, *_, desc in rows),
                   key=lambda r: hashlib.md5(b"audit-sheet:" + r[0].encode()).hexdigest())
    for path in RATERS:
        write_csv(path, SHEET, blind)


def category(injection, syntax):
    return NONE if injection == "no" else syntax


def read_labels(path, inj_col="injection", syn_col="syntax"):
    """Return {cve_id: category} for graded rows and the ids still blank.

    An invalid value stops the read: a typo must not count as a label.
    """
    graded, blank = {}, []
    with open(path, newline="") as f:
        for line, row in enumerate(csv.DictReader(f), start=2):
            inj = row[inj_col].strip().lower()
            syn = row[syn_col].strip().lower()
            where = f"{path.relative_to(ROOT)}:{line} ({row['cve_id']})"
            if not inj and not syn:
                blank.append(row["cve_id"])
            elif inj == "no" and not syn:
                graded[row["cve_id"]] = NONE
            elif inj == "yes" and syn in SYNTAX_VALUES:
                graded[row["cve_id"]] = syn
            else:
                raise ValueError(f"{where}: injection={inj!r} syntax={syn!r}; expected "
                                 f"'no' with no syntax, or 'yes' with one of "
                                 f"{sorted(SYNTAX_VALUES)}")
    return graded, blank


def kappa(a, b):
    """Cohen's kappa of two raters over the same items."""
    n = len(a)
    observed = sum(a[k] == b[k] for k in a) / n
    ca, cb = Counter(a.values()), Counter(b.values())
    expected = sum(ca[c] * cb[c] for c in ca) / (n * n)
    return 1.0 if expected == 1 else (observed - expected) / (1 - expected)


def gold():
    """Prefill gold.csv: agreed rows are filled, disagreements stay blank for
    the raters to settle together."""
    if GOLD.exists():
        sys.exit(f"{GOLD.relative_to(ROOT)} exists; it holds the settled labels")
    (a, blank_a), (b, blank_b) = (read_labels(p) for p in RATERS)
    if blank_a or blank_b:
        sys.exit(f"raters not done: {len(blank_a)} and {len(blank_b)} rows blank")
    rows, split = [], 0
    for cid in sorted(a):
        agreed = a[cid] == b[cid]
        split += not agreed
        inj, syn = (("no", "") if a[cid] == NONE else ("yes", a[cid])) if agreed else ("", "")
        rows.append((cid, inj, syn, a[cid], b[cid]))
    write_csv(GOLD, ["cve_id", "injection", "syntax", "rater_a", "rater_b"], rows)
    print(f"{split} disagreements left blank; kappa {kappa(a, b):.2f}")


def wilson(k, n, empty, z=1.96):
    """Point estimate and 95% Wilson interval; an empty stratum takes the point
    `empty` and spans [0, 1]."""
    if n == 0:
        return empty, 0.0, 1.0
    p = k / n
    centre = (p + z * z / (2 * n)) / (1 + z * z / n)
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / (1 + z * z / n)
    return p, max(0.0, centre - half), min(1.0, centre + half)


def metrics(con):
    """Audit metrics from the settled labels, or None until gold.csv is complete.

    Rates come from the frozen sample; they are applied to the current count of
    each layer, split by whether the pipeline label is matcher-delimitable.
    """
    if not GOLD.exists():
        return None
    labels, blank = read_labels(GOLD)
    if blank:
        return None
    with open(SAMPLE, newline="") as f:
        sample = {r["cve_id"]: (r["method"], r["syntax_type"]) for r in csv.DictReader(f)}
    host = matchertext.HOST
    strata = defaultdict(Counter)
    for cid, cat in labels.items():
        method, syn = sample[cid]
        if cat == "unclear":
            strata[method]["unclear"] += 1
            continue
        s = strata[method]
        s["graded"] += 1
        s["gate"] += cat != NONE
        s["correct"] += cat == syn
        side = "host" if syn in host else "rest"
        s[side] += 1
        s[f"{side}_flip"] += (cat in host) != (side == "host")

    counts = defaultdict(Counter)
    for method, syn, n in con.execute("""SELECT method, syntax_type, COUNT(*)
                                         FROM classification GROUP BY 1, 2"""):
        counts[method]["host" if syn in host else "rest"] += n
        counts[method]["all"] += n

    out = {"graded": sum(s["graded"] for s in strata.values()), "layers": {}}
    hostable = [0.0, 0.0, 0.0]   # point, low, high
    injection = [0.0, 0.0, 0.0]
    for method in LAYERS:
        s, c = strata[method], counts[method]
        # With no graded row a layer is taken as all injection and flip-free.
        gate = wilson(s["gate"], s["graded"], 1.0)
        out_host = wilson(s["host_flip"], s["host"], 0.0)  # labeled host, truly not
        in_rest = wilson(s["rest_flip"], s["rest"], 0.0)   # labeled not host, truly host
        out["layers"][method] = {
            "graded": s["graded"],
            "unclear": s["unclear"],
            # The unknown layer carries no syntax to be right about.
            "precision": None if method == "unknown"
            else wilson(s["correct"], s["graded"], 0.0),
            "gate": gate,
            "flip_out": out_host,
            "flip_in": in_rest,
        }
        # Low ceiling: most host labels wrong, fewest missed; high the reverse.
        for i, (fo, fi) in enumerate(((out_host[0], in_rest[0]),
                                      (out_host[2], in_rest[1]),
                                      (out_host[1], in_rest[2]))):
            hostable[i] += c["host"] * (1 - fo) + c["rest"] * fi
        for i, g in enumerate((gate[0], gate[2], gate[1])):
            injection[i] += c["all"] * g
    out["hostable_share"] = hostable[0] / injection[0]
    out["hostable_low"] = hostable[1] / injection[1]
    out["hostable_high"] = hostable[2] / injection[2]

    a, blank_a = read_labels(RATERS[0])
    b, blank_b = read_labels(RATERS[1])
    out["kappa"] = None if blank_a or blank_b else kappa(a, b)
    return out


def report(con):
    m = metrics(con)
    if m is None:
        sys.exit(f"{GOLD.relative_to(ROOT)} missing or incomplete")
    pct = lambda r: f"{r[0]:6.1%} [{r[1]:.1%}, {r[2]:.1%}]"
    print(f"graded {m['graded']}, kappa "
          + ("n/a" if m["kappa"] is None else f"{m['kappa']:.2f}"))
    for method, l in m["layers"].items():
        precision = "n/a" if l["precision"] is None else pct(l["precision"])
        print(f"{method:8} n={l['graded']:3} unclear={l['unclear']:2}  precision {precision}  "
              f"gate {pct(l['gate'])}  host->not {pct(l['flip_out'])}  "
              f"not->host {pct(l['flip_in'])}")
    print(f"adjusted ceiling {m['hostable_share']:.1%} "
          f"[{m['hostable_low']:.1%}, {m['hostable_high']:.1%}]")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=("sheets", "gold", "report"))
    args = ap.parse_args()
    if args.command == "gold":
        gold()
        return
    con = sqlite3.connect(f"file:{DB}?mode=ro", uri=True)
    if args.command == "sheets":
        sheets(con)
    else:
        report(con)


if __name__ == "__main__":
    main()
