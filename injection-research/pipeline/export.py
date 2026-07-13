"""Stage 4: analysis exports (CSV) from data/cve.db into data/exports/."""
import argparse
import csv
import hashlib
import sqlite3
import statistics
from collections import defaultdict
from pathlib import Path

from taxonomy import labels_from_cwes

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
EXPORTS = ROOT / "data" / "exports"

YEAR = "COALESCE(NULLIF(substr(v.published_at,1,4),''), substr(v.cve_id,5,4))"


def write(name, header, rows):
    with open(EXPORTS / name, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)
    print(f"wrote {name}")


def kev_epss_by_class(con):
    groups = defaultdict(lambda: [0, 0, []])  # n, kev_n, epss scores
    for fam, syn, kev, score in con.execute("""
            SELECT c.weakness_family, c.syntax_type, k.cve_id IS NOT NULL, e.score
            FROM classification c LEFT JOIN kev k USING(cve_id)
            LEFT JOIN epss e USING(cve_id)"""):
        g = groups[(fam, syn)]
        g[0] += 1
        g[1] += kev
        if score is not None:
            g[2].append(score)
    rows = []
    for (fam, syn), (n, kev_n, scores) in sorted(groups.items()):
        p90 = min(statistics.quantiles(scores, n=10)[8], max(scores)) if len(scores) > 1 \
            else (scores[0] if scores else "")
        rows.append([fam, syn, n, kev_n, round(kev_n / n, 4),
                     round(statistics.fmean(scores), 5) if scores else "",
                     round(statistics.median(scores), 5) if scores else "",
                     round(p90, 5) if scores else ""])
    write("kev_epss_by_class.csv",
          ["weakness_family", "syntax_type", "n", "kev_n", "kev_share",
           "epss_mean", "epss_median", "epss_p90"], rows)


def enrichment_by_year(con):
    flags = {}
    for cid, cna, adp, nvd in con.execute("""
            SELECT cve_id, MAX(source='cna'), MAX(source='adp_cisa'), MAX(source='nvd')
            FROM cwe_assignment GROUP BY cve_id"""):
        flags[cid] = (cna, adp, nvd)
    has_cvss = {cid for (cid,) in con.execute("SELECT DISTINCT cve_id FROM cvss")}
    nvd_enriched = dict(con.execute("SELECT cve_id, enriched FROM nvd_status"))
    agg = defaultdict(lambda: [0] * 6)
    for cid, year in con.execute(
            f"SELECT v.cve_id, {YEAR} FROM cve v WHERE v.state='PUBLISHED'"):
        a = agg[year]
        cna, adp, nvd = flags.get(cid, (0, 0, 0))
        a[0] += 1
        a[1] += cna
        a[2] += adp
        a[3] += nvd
        a[4] += cid in has_cvss
        a[5] += nvd_enriched.get(cid, 0)
    write("enrichment_by_year.csv",
          ["year", "n_published", "cna_cwe", "adp_cwe", "nvd_cwe", "any_cvss", "nvd_enriched"],
          [[y, *a] for y, a in sorted(agg.items())])


def cwe_disagreement(con):
    parents = defaultdict(list)
    for c, p in con.execute("SELECT child_id, parent_id FROM cwe_hierarchy"):
        parents[c].append(p)
    per_source = defaultdict(lambda: defaultdict(list))
    for cid, cwe, src in con.execute("""
            SELECT a.cve_id, a.cwe_id, a.source FROM cwe_assignment a
            JOIN classification USING(cve_id)"""):
        per_source[cid][src].append(cwe)
    rows = []
    for cid, by_src in per_source.items():
        fams = {src: labels_from_cwes(sorted(ids), parents)[0] for src, ids in by_src.items()}
        distinct = {f for f in fams.values() if f}
        if len(distinct) > 1:
            rows.append([cid, fams.get("cna", ""), fams.get("adp_cisa", ""), fams.get("nvd", "")])
    write("cwe_disagreement.csv",
          ["cve_id", "cna_family", "adp_family", "nvd_family"], sorted(rows))


def audit_sample(con, per_method=75):
    rows = []
    for (method,) in con.execute("SELECT DISTINCT method FROM classification ORDER BY method"):
        sample = sorted(
            con.execute("""SELECT c.cve_id, c.weakness_family, c.syntax_type, c.method,
                                  c.confidence, substr(v.description, 1, 500)
                           FROM classification c JOIN cve v USING(cve_id)
                           WHERE c.method=?""", (method,)),
            key=lambda r: hashlib.md5(r[0].encode()).hexdigest())[:per_method]
        rows.extend(sample)
    write("audit_sample.csv",
          ["cve_id", "weakness_family", "syntax_type", "method", "confidence", "description"],
          rows)


def coverage(con):
    q = lambda sql: con.execute(sql).fetchone()[0]
    rows = [
        ["kev_rows", q("SELECT COUNT(*) FROM kev")],
        ["kev_joined_to_cve", q("SELECT COUNT(*) FROM kev JOIN cve USING(cve_id)")],
        ["published_cves", q("SELECT COUNT(*) FROM cve WHERE state='PUBLISHED'")],
        ["published_with_epss", q("""SELECT COUNT(*) FROM cve v JOIN epss USING(cve_id)
                                     WHERE v.state='PUBLISHED'""")],
        ["injection_related", q("SELECT COUNT(*) FROM classification")],
        ["injection_with_poc", q("""SELECT COUNT(DISTINCT cve_id) FROM poc
                                    WHERE cve_id IN (SELECT cve_id FROM classification)""")],
        ["sanity_xss_n", q("SELECT COUNT(*) FROM classification WHERE syntax_type='html_dom'")],
        ["sanity_sqli_n", q("SELECT COUNT(*) FROM classification WHERE syntax_type='sql'")],
    ]
    write("coverage.csv", ["metric", "value"], rows)


def poc_by_source(con):
    write("poc_by_source.csv", ["source", "poc_rows", "distinct_cves", "injection_cves"],
          con.execute("""SELECT p.source, COUNT(*), COUNT(DISTINCT p.cve_id),
                                COUNT(DISTINCT c.cve_id)
                         FROM poc p LEFT JOIN classification c USING(cve_id)
                         GROUP BY 1 ORDER BY 2 DESC"""))


def run(args):
    EXPORTS.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(DB)

    write("yearly_totals.csv", ["year", "total_cves", "injection_related"], con.execute(f"""
        SELECT {YEAR}, COUNT(*), COUNT(c.cve_id)
        FROM cve v LEFT JOIN classification c USING(cve_id)
        WHERE v.state='PUBLISHED' GROUP BY 1 ORDER BY 1"""))
    write("yearly_family.csv", ["year", "weakness_family", "count"], con.execute(f"""
        SELECT {YEAR}, c.weakness_family, COUNT(*)
        FROM classification c JOIN cve v USING(cve_id) GROUP BY 1, 2 ORDER BY 1, 2"""))
    write("yearly_syntax.csv", ["year", "syntax_type", "count"], con.execute(f"""
        SELECT {YEAR}, c.syntax_type, COUNT(*)
        FROM classification c JOIN cve v USING(cve_id) GROUP BY 1, 2 ORDER BY 1, 2"""))
    write("method_by_year.csv", ["year", "method", "count"], con.execute(f"""
        SELECT {YEAR}, c.method, COUNT(*)
        FROM classification c JOIN cve v USING(cve_id) GROUP BY 1, 2 ORDER BY 1, 2"""))

    kev_epss_by_class(con)
    enrichment_by_year(con)
    cwe_disagreement(con)
    poc_by_source(con)
    audit_sample(con)
    coverage(con)
    con.close()


if __name__ == "__main__":
    run(argparse.ArgumentParser(description=__doc__).parse_args())
