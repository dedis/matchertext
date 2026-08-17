"""Stage 3d: measure matchertext prevention on the payload corpora and check the
CWE taxonomy against labeled ground truth.

Two things the CVE record alone cannot supply:

  1. Prevention rate. The CVE-derived measurement covers only the CVEs with an
     extractable PoC payload, a few thousand records. fuzzdb, PayloadsAllTheThings
     and SecLists supply attack strings directly, per sink type, so every payload
     in a matcher-hostable syntax can be run through VERIFY (matchertext.assess)
     instead of only those a PoC file happened to expose.

  2. Anchor coverage. OWASP Benchmark and the SARD Juliet suites label cases with
     a CWE and, for Benchmark, a verdict. Resolving those CWEs through the same
     FAMILY_ANCHORS/SYNTAX_ANCHORS walk used on real CVEs shows whether the
     taxonomy resolves the weakness classes that a purpose-built injection suite
     considers in scope, independently of how CNAs happen to assign CWEs.

Both are corpus-level measurements; neither modifies the CVE classification.
"""
import argparse
import csv
import sqlite3
from collections import Counter, defaultdict
from pathlib import Path

import matchertext
from classify import load_hierarchy
from syntactic_group import skeleton
from taxonomy import labels_from_cwes

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
EXPORTS = ROOT / "data" / "exports"


def assess_payloads(con):
    """Run VERIFY over every corpus payload; store one verdict per payload."""
    rows, tally = [], defaultdict(Counter)
    for corpus, syn, payload in con.execute(
            "SELECT corpus, syntax_type, payload FROM payload"):
        sk = skeleton(payload)
        preventable, mech, _ = matchertext.assess(syn, sk)
        rows.append((corpus, syn, payload, sk, int(preventable), mech or None))
        tally[syn][mech if preventable else "not_applicable"] += 1
    con.executescript("""
        DROP TABLE IF EXISTS payload_assessment;
        CREATE TABLE payload_assessment(corpus TEXT, syntax_type TEXT, payload TEXT,
            skeleton TEXT, preventable INTEGER, mechanism TEXT);
    """)
    con.executemany("INSERT INTO payload_assessment VALUES(?,?,?,?,?,?)", rows)
    con.execute("CREATE INDEX idx_pa_syn ON payload_assessment(syntax_type)")
    con.commit()
    return len(rows), tally


def check_ground_truth(con):
    """Resolve each labeled suite CWE through the taxonomy anchors."""
    parents, _ = load_hierarchy(con)
    rows = []
    for suite, cwe, n in con.execute(
            """SELECT suite, cwe_id, COUNT(*) FROM ground_truth
               WHERE cwe_id IS NOT NULL GROUP BY suite, cwe_id"""):
        fam, generic, syn = labels_from_cwes([cwe], parents)
        rows.append((suite, cwe, n, fam or "", int(bool(generic)), syn or ""))
    return rows


def run(args):
    con = sqlite3.connect(DB)
    EXPORTS.mkdir(parents=True, exist_ok=True)

    total, tally = assess_payloads(con)
    with open(EXPORTS / "payload_prevention.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["syntax_type", "payloads", "inert", "reject",
                    "not_applicable", "prevention_rate"])
        for syn in sorted(tally):
            c = tally[syn]
            n = sum(c.values())
            prevented = c["inert"] + c["reject"]
            w.writerow([syn, n, c["inert"], c["reject"], c["not_applicable"],
                        round(prevented / n, 4) if n else ""])

    gt = check_ground_truth(con)
    with open(EXPORTS / "ground_truth_anchors.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["suite", "cwe_id", "cases", "family", "family_generic", "syntax"])
        w.writerows(sorted(gt, key=lambda r: (r[0], -r[2])))

    hosted = {s: sum(c.values()) for s, c in tally.items() if s in matchertext.HOST}
    prevented = sum(tally[s]["inert"] + tally[s]["reject"] for s in hosted)
    n_hosted = sum(hosted.values())
    print(f"payloads assessed: {total}; matcher-hostable syntaxes: {n_hosted}; "
          f"prevented: {prevented}"
          + (f" ({prevented / n_hosted:.1%})" if n_hosted else ""))
    resolved = sum(1 for r in gt if r[3] and not r[4])
    print(f"ground truth: {len(gt)} (suite, cwe) pairs, "
          f"{resolved} resolve to a specific family")
    con.close()


if __name__ == "__main__":
    run(argparse.ArgumentParser(description=__doc__).parse_args())
