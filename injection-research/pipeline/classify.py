"""Stage 3: label published CVEs with injection relevance, weakness family, syntax type.

Fallback chain per record: CWE anchors -> phrase rules -> word naive Bayes
(margin-thresholded) -> unknown. The method column records which stage
determined the syntax type.

The classifier also learns a NOT_INJECTION class from the published CVEs that
the corpus gate rejects. Many CVEs reach it through a generic CWE such as
CWE-74 without being injection; when that class wins, the CVE stays unknown
rather than receive a syntax it does not have.
"""
import argparse
import csv
import functools
import hashlib
import re
import sqlite3
from collections import defaultdict
from pathlib import Path

import matchertext
from nbayes import WordNB
from taxonomy import (FAMILY_ANCHORS, FAMILY_OF_SYNTAX, LLM_MEDIATED, MEMORY_CORRUPTION,
                      NOT_INJECTION, OTHER, REVIEWED_INHERITED, SYNTAX_ANCHORS,
                      labels_from_cwes, mask_rules, rule_match)

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
EXPORTS = ROOT / "data" / "exports"
SOURCE_PRIORITY = {"cna": 0, "adp_cisa": 1, "nvd": 2, "ghsa": 3, "redhat": 4, "nuclei": 5}
# The CVE record and NVD. The other sources count only for a CVE that these
# give no CWE, so a secondary source cannot overrule them.
PRIMARY_SOURCES = {"cna", "adp_cisa", "nvd"}
NOT_INJECTION_CLASS = "none"


def bucket(text):
    """The holdout split of a description. Descriptions that differ only in numbers, case, or spacing (one
    vendor's template for many CVEs) share a split, so the holdout does not test on a copy of a training text."""
    key = " ".join(re.sub(r"\d+(?:\.\d+)*", " ", text.lower()).split())
    return int(hashlib.md5(key.encode()).hexdigest(), 16) % 10


def load_hierarchy(con):
    parents, children = defaultdict(list), defaultdict(list)
    for c, p in con.execute("SELECT child_id, parent_id FROM cwe_hierarchy"):
        parents[c].append(p)
        children[p].append(c)
    # The walk passes through NOT_INJECTION, so a CWE added below one of them
    # still meets the anchor audit.
    seen, stack = {74}, [74]
    while stack:
        for c in children.get(stack.pop(), ()):
            if c not in seen:
                seen.add(c)
                stack.append(c)
    return parents, (seen - NOT_INJECTION) | set(FAMILY_ANCHORS)


def anchor_audit(con, parents, subtree):
    """List every CWE the taxonomy labels, and whether it carries its own
    anchor or inherits its labels from an ancestor. An inherited label is where
    a CWE added to the catalogue can land in the wrong class, as CWE-1427
    (LLM prompt injection) did under CWE-77 (command injection). Stops when a
    CWE with CVEs inherits labels that REVIEWED_INHERITED does not list."""
    n_cve = dict(con.execute(
        "SELECT cwe_id, COUNT(DISTINCT cve_id) FROM cwe_assignment GROUP BY 1"))
    rows, unreviewed = [], []
    for cwe in subtree:
        fam, generic, syn = labels_from_cwes([cwe], parents)
        direct = cwe in FAMILY_ANCHORS or cwe in SYNTAX_ANCHORS
        if (not direct and n_cve.get(cwe)
                and REVIEWED_INHERITED.get(cwe) != (fam, generic, syn)):
            unreviewed.append(cwe)
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
    if unreviewed:
        raise SystemExit(
            f"anchor audit: no review accepts the inherited labels of CWE "
            f"{', '.join(map(str, sorted(unreviewed)))}; check them in "
            f"{EXPORTS / 'anchor_audit.csv'}, then anchor them or list them in "
            f"taxonomy.REVIEWED_INHERITED")


def mediated(syn, desc):
    """The syntax, or llm_prompt where a language model writes the
    delimitable syntax from the attacker's prompt (taxonomy.LLM_MEDIATED)."""
    return "llm_prompt" if syn in matchertext.HOST and LLM_MEDIATED.search(desc) else syn


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
    syntax_of = functools.cache(lambda cwe: labels_from_cwes([cwe], parents)[2])

    primary, secondary = defaultdict(set), defaultdict(set)
    for cve_id, cwe, src in con.execute("SELECT cve_id, cwe_id, source FROM cwe_assignment"):
        (primary if src in PRIMARY_SOURCES else secondary)[cve_id].add(
            (SOURCE_PRIORITY.get(src, 9), cwe))

    EXPORTS.mkdir(parents=True, exist_ok=True)
    anchor_audit(con, parents, subtree)

    results = {}  # cve_id -> [family, syntax, method, confidence]
    train, pending = [], []
    overridden = contradicted = abstained = memory = 0
    relabeled = []  # (cve_id, syntax) a language model writes, now llm_prompt
    for cve_id, desc in con.execute(
            "SELECT cve_id, COALESCE(description,'') FROM cve WHERE state='PUBLISHED'"):
        rm = rule_match(desc) if desc else None
        # Within one source, a CWE of the syntax that the description names comes first, then the lower number.
        named = rm[1] if rm else None
        ids = [c for *_, c in sorted((p, named is not None and syntax_of(c) != named, c)
                                     for p, c in primary.get(cve_id) or secondary.get(cve_id, ()))]
        admitted = rm or any(c in subtree for c in ids)
        if admitted:
            fam, generic, syn = labels_from_cwes(ids, parents)
            if not rm and syn == "code_eval" and MEMORY_CORRUPTION.search(desc):
                memory += 1
                admitted = False
        if not admitted:
            if desc:
                train.append((cve_id, desc, NOT_INJECTION_CLASS))
            continue
        method = conf = None
        if syn is not None:
            method = "cwe"
        elif rm:
            syn, method = rm[1], "rule"
        if method and (mt := mediated(syn, desc)) != syn:
            relabeled.append((cve_id, syn))
            syn = mt
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
        tr = [(t, y) for _, t, y in train if bucket(t) != 0]
        te = [(t, y) for _, t, y in train if bucket(t) == 0]
        stats, covered = evaluate(WordNB().fit(*zip(*tr)), te, margin)
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

        model = WordNB().fit([t for _, t, _ in train], [y for _, _, y in train])
        for cve_id, desc, refinable in pending:
            y, m = model.predict(desc)
            if y == NOT_INJECTION_CLASS:
                abstained += 1
            elif m >= margin:
                mt = mediated(y, desc)
                r = results[cve_id]
                if refinable:
                    r[0] = FAMILY_OF_SYNTAX[mt]
                elif FAMILY_OF_SYNTAX[mt] != r[0]:
                    contradicted += 1     # a specific CWE family outranks the classifier
                    continue
                if mt != y:
                    relabeled.append((cve_id, y))
                r[1], r[2], r[3] = mt, "nb", round(m, 2)

    con.executescript("""
        DROP TABLE IF EXISTS classification;
        CREATE TABLE classification(cve_id TEXT PRIMARY KEY, injection_related INTEGER,
            weakness_family TEXT, syntax_type TEXT, method TEXT, confidence REAL);
    """)
    con.executemany("INSERT INTO classification VALUES(?,?,?,?,?,?)",
                    [(cid, 1, *r) for cid, r in results.items()])
    con.commit()
    with open(EXPORTS / "llm_relabeled.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["cve_id", "syntax_before"])
        w.writerows(sorted(relabeled))

    counts = dict(con.execute(
        "SELECT method, COUNT(*) FROM classification GROUP BY method"))
    total = con.execute("SELECT COUNT(*) FROM cve WHERE state='PUBLISHED'").fetchone()[0]
    print(f"injection-related: {len(results)} of {total} published CVEs; by method: {counts}")
    print(f"family set by syntax: {overridden}; classifier labels refused for "
          f"contradicting the CWE family: {contradicted}; left unknown as not "
          f"injection: {abstained}; code-eval CWE on memory corruption, dropped: {memory}; "
          f"delimitable syntax written by a language model, relabeled llm_prompt: {len(relabeled)}")
    con.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--nb-margin", type=float, default=3.0,
                    help="minimum NB log-odds margin to accept a label")
    run(ap.parse_args())
