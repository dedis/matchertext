"""Stage 3: label published CVEs with injection relevance, weakness family, syntax type.

Fallback chain per record: CWE anchors -> phrase rules -> trigram naive Bayes
(margin-thresholded) -> unknown. The method column records which stage
determined the syntax type.
"""
import argparse
import csv
import hashlib
import sqlite3
from collections import defaultdict
from pathlib import Path

from nbayes import TrigramNB
from taxonomy import (FAMILY_ANCHORS, FAMILY_OF_SYNTAX, OTHER, SYNTAX_ANCHORS,
                      labels_from_cwes, mask_rules, rule_match)

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
EXPORTS = ROOT / "data" / "exports"
SOURCE_PRIORITY = {"cna": 0, "adp_cisa": 1, "nvd": 2, "ghsa": 3, "redhat": 4, "nuclei": 5}


def bucket(cve_id):
    return int(hashlib.md5(cve_id.encode()).hexdigest(), 16) % 10


def load_hierarchy(con):
    parents, children = defaultdict(list), defaultdict(list)
    for c, p in con.execute("SELECT child_id, parent_id FROM cwe_hierarchy"):
        parents[c].append(p)
        children[p].append(c)
    seen, stack = {74}, [74]
    while stack:
        for c in children.get(stack.pop(), ()):
            if c not in seen:
                seen.add(c)
                stack.append(c)
    return parents, seen | set(FAMILY_ANCHORS)


def anchor_audit(con, parents, subtree):
    """List every CWE the taxonomy labels, and whether it carries its own
    anchor or inherits its labels from an ancestor. An inherited label is where
    a CWE added to the catalogue can land in the wrong class, as CWE-1427
    (LLM prompt injection) did under CWE-77 (command injection)."""
    n_cve = dict(con.execute(
        "SELECT cwe_id, COUNT(DISTINCT cve_id) FROM cwe_assignment GROUP BY 1"))
    rows = []
    for cwe in subtree:
        fam, generic, syn = labels_from_cwes([cwe], parents)
        direct = cwe in FAMILY_ANCHORS or cwe in SYNTAX_ANCHORS
        rows.append((cwe, n_cve.get(cwe, 0), fam or "", int(bool(generic)),
                     syn or "", int(direct)))
    rows.sort(key=lambda r: (r[5], -r[1], r[0]))
    with open(EXPORTS / "anchor_audit.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["cwe_id", "cves", "family", "family_generic", "syntax", "own_anchor"])
        w.writerows(rows)
    inherited = [r for r in rows if not r[5]]
    print(f"anchor audit: {len(inherited)} of {len(rows)} labelled CWEs inherit their "
          f"labels, covering {sum(r[1] for r in inherited)} CWE assignments")


def evaluate(model, test, margin):
    stats = defaultdict(lambda: [0, 0, 0])  # tp, fp, fn
    covered = 0
    for text, y in test:
        p, m = model.predict(text)
        if m < margin:
            stats[y][2] += 1
            continue
        covered += 1
        if p == y:
            stats[y][0] += 1
        else:
            stats[p][1] += 1
            stats[y][2] += 1
    return stats, covered


def run(args):
    margin = getattr(args, "nb_margin", 3.0)
    con = sqlite3.connect(DB)
    parents, subtree = load_hierarchy(con)

    cwes = defaultdict(list)
    for cve_id, cwe, src in con.execute("SELECT cve_id, cwe_id, source FROM cwe_assignment"):
        cwes[cve_id].append((SOURCE_PRIORITY.get(src, 9), cwe))

    EXPORTS.mkdir(parents=True, exist_ok=True)
    anchor_audit(con, parents, subtree)

    results = {}  # cve_id -> [family, syntax, method, confidence]
    train, pending = [], []
    overridden = contradicted = 0
    for cve_id, desc in con.execute(
            "SELECT cve_id, COALESCE(description,'') FROM cve WHERE state='PUBLISHED'"):
        ids = [c for _, c in sorted(set(cwes.get(cve_id, ())))]
        rm = rule_match(desc) if desc else None
        if not (rm or any(c in subtree for c in ids)):
            continue
        fam, generic, syn = labels_from_cwes(ids, parents)
        method = conf = None
        if syn is not None:
            method = "cwe"
        elif rm:
            syn, method = rm[1], "rule"
        if rm and (fam is None or (fam == OTHER and generic)):
            fam, generic = rm[0], False
        if method:
            # The syntax is the more specific label, so it decides the family
            # where the two come from different CWEs or methods.
            if FAMILY_OF_SYNTAX[syn] != fam:
                fam, generic = FAMILY_OF_SYNTAX[syn], False
                overridden += 1
            if desc:
                # The rule phrases are blanked: the classifier only sees
                # descriptions without them, so it must not learn from them.
                train.append((cve_id, mask_rules(desc), syn))
        elif desc:
            pending.append((cve_id, desc, fam is None or generic))
        results[cve_id] = [fam or OTHER, syn or "unknown", method or "unknown", conf]

    if pending and train:
        tr = [(t, y) for cid, t, y in train if bucket(cid) != 0]
        te = [(t, y) for cid, t, y in train if bucket(cid) == 0]
        stats, covered = evaluate(TrigramNB().fit(*zip(*tr)), te, margin)
        with open(EXPORTS / "nb_holdout.csv", "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["class", "test_n", "precision", "recall", "coverage"])
            for c in sorted(stats):
                tp, fp, fn = stats[c]
                w.writerow([c, tp + fn,
                            round(tp / (tp + fp), 3) if tp + fp else "",
                            round(tp / (tp + fn), 3) if tp + fn else "",
                            round(covered / len(te), 3)])
        print(f"nb holdout: {len(te)} test docs, {covered} above margin {margin}")

        model = TrigramNB().fit([t for _, t, _ in train], [y for _, _, y in train])
        for cve_id, desc, refinable in pending:
            y, m = model.predict(desc)
            if m >= margin:
                r = results[cve_id]
                if refinable:
                    r[0] = FAMILY_OF_SYNTAX[y]
                elif FAMILY_OF_SYNTAX[y] != r[0]:
                    contradicted += 1     # a specific CWE family outranks the classifier
                    continue
                r[1], r[2], r[3] = y, "nb", round(m, 2)

    con.executescript("""
        DROP TABLE IF EXISTS classification;
        CREATE TABLE classification(cve_id TEXT PRIMARY KEY, injection_related INTEGER,
            weakness_family TEXT, syntax_type TEXT, method TEXT, confidence REAL);
    """)
    con.executemany("INSERT INTO classification VALUES(?,?,?,?,?,?)",
                    [(cid, 1, *r) for cid, r in results.items()])
    con.commit()

    counts = dict(con.execute(
        "SELECT method, COUNT(*) FROM classification GROUP BY method"))
    total = con.execute("SELECT COUNT(*) FROM cve WHERE state='PUBLISHED'").fetchone()[0]
    print(f"injection-related: {len(results)} of {total} published CVEs; by method: {counts}")
    print(f"family set by syntax: {overridden}; classifier labels refused for "
          f"contradicting the CWE family: {contradicted}")
    con.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--nb-margin", type=float, default=3.0,
                    help="minimum NB log-odds margin to accept a label")
    run(ap.parse_args())
