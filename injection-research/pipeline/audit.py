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
import random
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
STRATA = AUDIT / "strata.csv"
RATERS = (AUDIT / "rater_a.csv", AUDIT / "rater_b.csv")
GOLD = AUDIT / "gold.csv"

SHEET = ["cve_id", "description", "injection", "syntax", "notes"]
SYNTAX_VALUES = (set(taxonomy.SYNTAXES) - {"unknown"}) | {"other", "unclear"}
NONE = "none"  # the category of a CVE graded as not injection
OUT = "out"    # the layer of a sampled CVE that the corpus gate has since dropped
BOOTSTRAP = 2000


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
    for path in (SAMPLE, STRATA, *RATERS):
        if path.exists():
            sys.exit(f"{path.relative_to(ROOT)} exists; delete it to draw a new sample")
    AUDIT.mkdir(exist_ok=True)
    rows = export.audit_rows(con)
    write_csv(SAMPLE, ["cve_id", "method", "syntax_type"],
              sorted((cid, method, syn) for cid, _, syn, method, _, _ in rows))
    # The sampling weights: a later relabeling moves CVEs between layers.
    write_csv(STRATA, ["method", "cves"],
              con.execute("SELECT method, COUNT(*) FROM classification GROUP BY 1 ORDER BY 1"))
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


def wilson(k, n, z=1.96):
    """Point estimate and 95% Wilson interval of k successes in n."""
    p = k / n
    centre = (p + z * z / (2 * n)) / (1 + z * z / n)
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / (1 + z * z / n)
    return p, max(0.0, centre - half), min(1.0, centre + half)


def ceiling(groups, cells):
    """Estimated matcher-delimitable share of true injection CVEs.

    groups maps (stratum, cell) to the weight of its rows and the shares of them
    that are truly host and truly injection. Within each cell of the current
    labeling (layer, host or not), the weighted shares scale the cell's CVE
    count. A row is truly host only if it is truly injection, so the result is a
    share in [0, 1].
    """
    num, den = defaultdict(float), defaultdict(float)
    inj, hst = defaultdict(float), defaultdict(float)
    for (_, cell), (weight, host, injection) in groups.items():
        den[cell] += weight
        inj[cell] += weight * injection
        hst[cell] += weight * host
    for cell, n in cells.items():
        num["host"] += n * hst[cell] / den[cell]
        num["injection"] += n * inj[cell] / den[cell]
    return num["host"] / num["injection"]


def metrics(con):
    """Audit metrics from the settled labels, or None until gold.csv is complete.

    The sample was drawn per labeling layer at the sizes in strata.csv; each
    sampled CVE is scored against its current label. The ceiling interval is a
    percentile Bayesian bootstrap: each (stratum, cell) group draws its shares
    of host, other injection, and not injection from a Dirichlet with the
    Jeffreys prior, so a group with no error in the sample still varies. A
    cell no sampled CVE falls in takes its labels at face value for the point
    estimate and draws from the prior alone.
    """
    if not GOLD.exists():
        return None
    truth, blank = read_labels(GOLD)
    if blank:
        return None
    with open(SAMPLE, newline="") as f:
        stratum = {r["cve_id"]: r["method"] for r in csv.DictReader(f)}
    with open(STRATA, newline="") as f:
        size = {r["method"]: int(r["cves"]) for r in csv.DictReader(f)}
    label = defaultdict(lambda: (OUT, None), (
        (cid, (method, syn)) for cid, method, syn in con.execute(
            "SELECT cve_id, method, syntax_type FROM classification")))
    side = lambda method, syn: (method, syn in matchertext.HOST)
    cells = Counter()
    for method, syn, n in con.execute(
            "SELECT method, syntax_type, COUNT(*) FROM classification GROUP BY 1, 2"):
        cells[side(method, syn)] += n

    graded = {cid: cat for cid, cat in truth.items() if cat != "unclear"}
    per_stratum = Counter(stratum[cid] for cid in graded)
    counts = defaultdict(Counter)  # (stratum, cell) -> host, rest, none
    for cid, cat in graded.items():
        kind = NONE if cat == NONE else "host" if cat in matchertext.HOST else "rest"
        counts[(stratum[cid], side(*label[cid]))][kind] += 1
    weight = {k: size[k[0]] / per_stratum[k[0]] * sum(c.values()) for k, c in counts.items()}
    for cell in cells:
        if not any(c == cell for _, c in counts):
            counts[(None, cell)] = Counter()
            weight[(None, cell)] = 1.0

    def shares(draw):
        out = {}
        for k, c in counts.items():
            if draw is None:
                if c:
                    n = sum(c.values())
                    out[k] = (weight[k], c["host"] / n, (c["host"] + c["rest"]) / n)
                else:
                    out[k] = (weight[k], float(k[1][1]), 1.0)
                continue
            host, rest, none = (draw(c[kind]) for kind in ("host", "rest", NONE))
            total = host + rest + none
            out[k] = (weight[k], host / total, (host + rest) / total)
        return out

    rng = random.Random(0)
    boot = sorted(ceiling(shares(lambda n: rng.gammavariate(n + 0.5, 1.0)), cells)
                  for _ in range(BOOTSTRAP))
    out = {"graded": len(graded), "unclear": len(truth) - len(graded),
           "hostable_share": ceiling(shares(None), cells),
           "hostable_low": boot[int(0.025 * BOOTSTRAP)],
           "hostable_high": boot[int(0.975 * BOOTSTRAP) - 1],
           "strata": {}, "precision": {},
           # sampled CVEs the corpus gate has since dropped, and how many of
           # them the raters found not injection
           "dropped": (sum(label[cid][0] == OUT for cid in graded),
                       sum(label[cid][0] == OUT and cat == NONE
                           for cid, cat in graded.items()))}
    for h in sorted(size):
        rows = [cat for cid, cat in graded.items() if stratum[cid] == h]
        out["strata"][h] = {"graded": len(rows),
                            "injection": wilson(sum(c != NONE for c in rows), len(rows))}
    # Precision of the layers that still label, against their current labels.
    for method in sorted({m for m, _ in cells} - {"unknown"}):
        rows = [(cat, label[cid][1]) for cid, cat in graded.items() if label[cid][0] == method]
        out["precision"][method] = wilson(sum(c == s for c, s in rows), len(rows))

    a, blank_a = read_labels(RATERS[0])
    b, blank_b = read_labels(RATERS[1])
    out["kappa"] = None if blank_a or blank_b else kappa(a, b)
    return out


def report(con):
    m = metrics(con)
    if m is None:
        sys.exit(f"{GOLD.relative_to(ROOT)} missing or incomplete")
    pct = lambda r: f"{r[0]:6.1%} [{r[1]:.1%}, {r[2]:.1%}]"
    print(f"graded {m['graded']}, unclear {m['unclear']}, kappa "
          + ("n/a" if m["kappa"] is None else f"{m['kappa']:.2f}"))
    for h, s in m["strata"].items():
        print(f"sampled from {h:8} n={s['graded']:3}  injection {pct(s['injection'])}")
    for method, p in m["precision"].items():
        print(f"precision {method:8} {pct(p)}")
    print(f"adjusted ceiling {m['hostable_share']:.1%} "
          f"[{m['hostable_low']:.1%}, {m['hostable_high']:.1%}]")
    print("sampled CVEs the gate now drops: {}, graded not injection: {}".format(*m["dropped"]))


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
