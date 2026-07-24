"""Stage 2: parse raw snapshots into data/cve.db.

Source separation is the invariant: every enrichment row carries a source
column (cna | adp_cisa | nvd); nothing is merged or overwritten here.
"""
import argparse
import csv
import gzip
import io
import json
import re
import sqlite3
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

csv.field_size_limit(1 << 24)
CVE_RE = re.compile(r"CVE-\d{4}-\d+")
CWE_RE = re.compile(r"CWE-(\d+)")
MSF_CVE_RE = re.compile(r"""['"]CVE['"]\s*,\s*['"](\d{4}-\d+)['"]""")

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "data" / "raw"
DB = ROOT / "data" / "cve.db"

DDL = """
DROP TABLE IF EXISTS cve;
DROP TABLE IF EXISTS cwe_assignment;
DROP TABLE IF EXISTS cvss;
DROP TABLE IF EXISTS kev;
DROP TABLE IF EXISTS epss;
DROP TABLE IF EXISTS nvd_status;
DROP TABLE IF EXISTS cwe_hierarchy;
DROP TABLE IF EXISTS poc;
DROP TABLE IF EXISTS advisory;
CREATE TABLE cve(cve_id TEXT PRIMARY KEY, state TEXT, published_at TEXT,
                 updated_at TEXT, assigner TEXT, description TEXT);
CREATE TABLE cwe_assignment(cve_id TEXT, cwe_id INTEGER, source TEXT,
                            UNIQUE(cve_id, cwe_id, source));
CREATE TABLE cvss(cve_id TEXT, version TEXT, base_score REAL, vector TEXT, source TEXT,
                  UNIQUE(cve_id, version, source));
CREATE TABLE kev(cve_id TEXT PRIMARY KEY, date_added TEXT, known_ransomware TEXT);
CREATE TABLE epss(cve_id TEXT PRIMARY KEY, score REAL, percentile REAL, model_date TEXT);
CREATE TABLE nvd_status(cve_id TEXT PRIMARY KEY, last_modified TEXT,
                        vuln_status TEXT, enriched INTEGER);
CREATE TABLE cwe_hierarchy(child_id INTEGER, parent_id INTEGER, UNIQUE(child_id, parent_id));
-- Proof-of-concept references. local_path is set for sources cloned locally
-- (exploitdb, nuclei, metasploit), so a payload can be extracted; NULL for
-- link-only sources (github).
CREATE TABLE poc(cve_id TEXT, source TEXT, ref TEXT, local_path TEXT, kind TEXT,
                 UNIQUE(cve_id, source, ref));
-- Per-source advisory metadata (severity / fix status) for breadth.
CREATE TABLE advisory(cve_id TEXT, source TEXT, severity TEXT, status TEXT, ref TEXT,
                      UNIQUE(cve_id, source, ref));
"""

CVSS_KEYS = ("cvssV4_0", "cvssV3_1", "cvssV3_0", "cvssV2_0")


def cwe_int(value):
    if value and value.upper().startswith("CWE-") and value[4:].isdigit():
        return int(value[4:])
    return None


def container_enrichment(cid, container, source, cwe_rows, cvss_rows):
    for pt in container.get("problemTypes") or ():
        for d in pt.get("descriptions") or ():
            n = cwe_int(d.get("cweId"))
            if n:
                cwe_rows.add((cid, n, source))
    for m in container.get("metrics") or ():
        for key in CVSS_KEYS:
            cd = m.get(key)
            if cd:
                cvss_rows.append((cid, cd.get("version"), cd.get("baseScore"),
                                  cd.get("vectorString"), source))


def iter_records(base, years):
    for ydir in sorted(base.iterdir()):
        if not ydir.is_dir() or not ydir.name.isdigit():
            continue
        if years and int(ydir.name) not in years:
            continue
        for sub in sorted(ydir.iterdir()):
            if not sub.is_dir():
                continue
            for f in sorted(sub.glob("CVE-*.json")):
                yield json.loads(f.read_bytes())


def load_cvelist(con, years):
    cve_rows, cwe_rows, cvss_rows = [], set(), []
    for rec in iter_records(RAW / "cvelistV5" / "cves", years):
        meta = rec["cveMetadata"]
        cid = meta["cveId"]
        cna = rec.get("containers", {}).get("cna", {})
        desc = next((d.get("value") for d in cna.get("descriptions") or ()
                     if d.get("lang", "").startswith("en")), None)
        cve_rows.append((cid, meta.get("state"), meta.get("datePublished"),
                         meta.get("dateUpdated"), meta.get("assignerShortName"), desc))
        container_enrichment(cid, cna, "cna", cwe_rows, cvss_rows)
        if len(cve_rows) >= 20000:
            flush(con, cve_rows, cwe_rows, cvss_rows)
    flush(con, cve_rows, cwe_rows, cvss_rows)


def load_vulnrichment(con, years):
    cwe_rows, cvss_rows = set(), []
    for rec in iter_records(RAW / "vulnrichment", years):
        cid = rec["cveMetadata"]["cveId"]
        for adp in rec.get("containers", {}).get("adp") or ():
            container_enrichment(cid, adp, "adp_cisa", cwe_rows, cvss_rows)
    flush(con, [], cwe_rows, cvss_rows)


def flush(con, cve_rows, cwe_rows, cvss_rows):
    con.executemany("INSERT OR REPLACE INTO cve VALUES(?,?,?,?,?,?)", cve_rows)
    con.executemany("INSERT OR IGNORE INTO cwe_assignment VALUES(?,?,?)", cwe_rows)
    con.executemany("INSERT OR IGNORE INTO cvss VALUES(?,?,?,?,?)", cvss_rows)
    con.commit()
    cve_rows.clear(), cwe_rows.clear(), cvss_rows.clear()


def load_nvd(con, years):
    for feed in sorted((RAW / "nvd").glob("nvdcve-2.0-*.json.gz")):
        year = int(feed.name.split("-")[2].split(".")[0])
        if years and year not in years:
            continue
        print(f"nvd {year}", flush=True)
        with gzip.open(feed, "rt", encoding="utf-8") as f:
            items = json.load(f)["vulnerabilities"]
        cwe_rows, cvss_rows, status_rows = set(), [], []
        for item in items:
            c = item["cve"]
            cid = c["id"]
            for w in c.get("weaknesses") or ():
                for d in w.get("description") or ():
                    n = cwe_int(d.get("value"))
                    if n:
                        cwe_rows.add((cid, n, "nvd"))
            metrics = c.get("metrics") or {}
            for key in ("cvssMetricV40", "cvssMetricV31", "cvssMetricV30", "cvssMetricV2"):
                lst = metrics.get(key) or []
                m = next((x for x in lst if x.get("type") == "Primary"), lst[0] if lst else None)
                if m:
                    cd = m["cvssData"]
                    cvss_rows.append((cid, cd.get("version"), cd.get("baseScore"),
                                      cd.get("vectorString"), "nvd"))
            enriched = 1 if (c.get("weaknesses") or c.get("configurations")) else 0
            status_rows.append((cid, c.get("lastModified"), c.get("vulnStatus"), enriched))
        con.executemany("INSERT OR IGNORE INTO cwe_assignment VALUES(?,?,?)", cwe_rows)
        con.executemany("INSERT OR IGNORE INTO cvss VALUES(?,?,?,?,?)", cvss_rows)
        con.executemany("INSERT OR REPLACE INTO nvd_status VALUES(?,?,?,?)", status_rows)
        con.commit()


def load_kev(con):
    data = json.loads((RAW / "kev.json").read_text())
    con.executemany("INSERT OR REPLACE INTO kev VALUES(?,?,?)",
                    [(v["cveID"], v.get("dateAdded"), v.get("knownRansomwareCampaignUse"))
                     for v in data["vulnerabilities"]])
    con.commit()


def load_epss(con):
    path = next((RAW / "epss").glob("epss_scores-*.csv.gz"))
    model_date = path.name[len("epss_scores-"):-len(".csv.gz")]
    with gzip.open(path, "rt", encoding="utf-8") as f:
        line = f.readline()
        if not line.startswith("#"):
            f.seek(0)
        rows = [(r["cve"], float(r["epss"]), float(r["percentile"]), model_date)
                for r in csv.DictReader(f)]
    con.executemany("INSERT OR REPLACE INTO epss VALUES(?,?,?,?)", rows)
    con.commit()


def load_cwe(con):
    def local(tag):
        return tag.rpartition("}")[2]
    rows = set()
    with zipfile.ZipFile(RAW / "cwe" / "cwec_latest.xml.zip") as z:
        with z.open(z.namelist()[0]) as f:
            for _, el in ET.iterparse(io.BufferedReader(f), events=("end",)):
                if local(el.tag) != "Weakness":
                    continue
                wid = int(el.get("ID"))
                for rel in el.iter():
                    if (local(rel.tag) == "Related_Weakness"
                            and rel.get("Nature") == "ChildOf"
                            and rel.get("View_ID", "1000") == "1000"):
                        rows.add((wid, int(rel.get("CWE_ID"))))
                el.clear()
    con.executemany("INSERT OR IGNORE INTO cwe_hierarchy VALUES(?,?)", rows)
    con.commit()


def _poc(con, rows):
    con.executemany("INSERT OR IGNORE INTO poc VALUES(?,?,?,?,?)", rows)
    con.commit()


def load_exploitdb(con):
    csv_path = RAW / "exploitdb" / "files_exploits.csv"
    if not csv_path.exists():
        print("exploitdb: not fetched, skipping")
        return
    rows = []
    with open(csv_path, encoding="utf-8", errors="replace", newline="") as f:
        for r in csv.DictReader(f):
            for cve in CVE_RE.findall(r.get("codes") or ""):
                rows.append((cve, "exploitdb", r["id"],
                             "exploitdb/" + r["file"], r.get("type")))
    _poc(con, rows)


def load_nuclei(con):
    """CVE-tagged detection templates: PoC payloads + CWE/CVSS enrichment."""
    base = RAW / "nuclei-templates"
    if not base.exists():
        print("nuclei: not fetched, skipping")
        return
    poc_rows, cwe_rows, cvss_rows = [], set(), []
    for f in base.rglob("CVE-*.yaml"):
        m = CVE_RE.search(f.name)
        if not m:
            continue
        cid = m.group(0)
        rel = str(f.relative_to(RAW))
        poc_rows.append((cid, "nuclei", str(f.relative_to(base)), rel, "template"))
        text = f.read_text(encoding="utf-8", errors="replace")
        for n in CWE_RE.findall(text):
            cwe_rows.add((cid, int(n), "nuclei"))
        cm = re.search(r"cvss-metrics:\s*(\S+)", text)
        cs = re.search(r"cvss-score:\s*([\d.]+)", text)
        if cm or cs:
            ver = "3.1" if cm and "3.1" in cm.group(1) else ("4.0" if cm and "4.0" in cm.group(1) else None)
            cvss_rows.append((cid, ver, float(cs.group(1)) if cs else None,
                              cm.group(1) if cm else None, "nuclei"))
    _poc(con, poc_rows)
    con.executemany("INSERT OR IGNORE INTO cwe_assignment VALUES(?,?,?)", cwe_rows)
    con.executemany("INSERT OR IGNORE INTO cvss VALUES(?,?,?,?,?)", cvss_rows)
    con.commit()


def load_metasploit(con):
    base = RAW / "metasploit-framework" / "modules"
    if not base.exists():
        print("metasploit: not fetched, skipping")
        return
    rows = []
    for f in base.rglob("*.rb"):
        text = f.read_text(encoding="utf-8", errors="replace")
        rel = str(f.relative_to(RAW))
        kind = f.relative_to(base).parts[0]
        for num in set(MSF_CVE_RE.findall(text)):
            rows.append(("CVE-" + num, "metasploit", str(f.relative_to(base)), rel, kind))
    _poc(con, rows)


def load_poc_github(con, per_cve=20):
    """nomi-sec PoC-in-GitHub: CVE -> public GitHub PoC repos (links only)."""
    base = RAW / "PoC-in-GitHub"
    if not base.exists():
        print("poc_github: not fetched, skipping")
        return
    rows = []
    for f in base.rglob("CVE-*.json"):
        cid = f.stem
        try:
            repos = json.loads(f.read_text(encoding="utf-8", errors="replace"))
        except (json.JSONDecodeError, OSError):
            continue
        repos.sort(key=lambda r: r.get("stargazers_count", 0), reverse=True)
        for r in repos[:per_cve]:
            url = r.get("html_url")
            if url:
                rows.append((cid, "github", url, None, "repo"))
    _poc(con, rows)


def _cwe_ints(value):
    return {int(n) for n in CWE_RE.findall(value or "")}


def load_ghsa(con):
    base = RAW / "advisory-database" / "advisories"
    if not base.exists():
        print("ghsa: not fetched, skipping")
        return
    cwe_rows, cvss_rows, adv_rows = set(), [], []
    for f in base.rglob("GHSA-*.json"):
        try:
            d = json.loads(f.read_text(encoding="utf-8", errors="replace"))
        except (json.JSONDecodeError, OSError):
            continue
        cves = [a for a in d.get("aliases") or () if a.startswith("CVE-")]
        if not cves:
            continue
        ghsa = d.get("id")
        spec = d.get("database_specific") or {}
        for cid in cves:
            for n in _cwe_ints(",".join(spec.get("cwe_ids") or ())):
                cwe_rows.add((cid, n, "ghsa"))
            for sev in d.get("severity") or ():
                vec = sev.get("score", "")
                ver = "4.0" if "CVSS:4.0" in vec else "3.1" if "CVSS:3" in vec else None
                cvss_rows.append((cid, ver, None, vec, "ghsa"))
            adv_rows.append((cid, "ghsa", spec.get("severity"), None, ghsa))
    con.executemany("INSERT OR IGNORE INTO cwe_assignment VALUES(?,?,?)", cwe_rows)
    con.executemany("INSERT OR IGNORE INTO cvss VALUES(?,?,?,?,?)", cvss_rows)
    con.executemany("INSERT OR IGNORE INTO advisory VALUES(?,?,?,?,?)", adv_rows)
    con.commit()


def load_redhat(con):
    path = RAW / "redhat" / "redhat_cve.json"
    if not path.exists():
        print("redhat: not fetched, skipping")
        return
    cwe_rows, cvss_rows, adv_rows = set(), [], []
    for r in json.loads(path.read_text(encoding="utf-8", errors="replace")):
        cid = r.get("CVE")
        if not cid or not cid.startswith("CVE-"):
            continue
        for n in _cwe_ints(r.get("CWE")):
            cwe_rows.add((cid, n, "redhat"))
        score, vec = r.get("cvss3_score"), r.get("cvss3_scoring_vector")
        if score or vec:
            cvss_rows.append((cid, "3.1", float(score) if score else None, vec, "redhat"))
        adv_rows.append((cid, "redhat", r.get("severity"), None, r.get("resource_url")))
    con.executemany("INSERT OR IGNORE INTO cwe_assignment VALUES(?,?,?)", cwe_rows)
    con.executemany("INSERT OR IGNORE INTO cvss VALUES(?,?,?,?,?)", cvss_rows)
    con.executemany("INSERT OR IGNORE INTO advisory VALUES(?,?,?,?,?)", adv_rows)
    con.commit()


def load_debian(con):
    path = RAW / "debian" / "debian_security.json"
    if not path.exists():
        print("debian: not fetched, skipping")
        return
    status = {}  # cve -> resolved if any release resolved, else open
    for pkg, cves in json.loads(path.read_text(encoding="utf-8", errors="replace")).items():
        for cid, info in cves.items():
            if not cid.startswith("CVE-"):
                continue
            resolved = any(rel.get("status") == "resolved"
                           for rel in (info.get("releases") or {}).values())
            status[cid] = "resolved" if resolved or status.get(cid) == "resolved" else "open"
    con.executemany("INSERT OR IGNORE INTO advisory VALUES(?,?,?,?,?)",
                    [(cid, "debian", None, st, None) for cid, st in status.items()])
    con.commit()


def run(args):
    years = set(getattr(args, "years", None) or ())
    DB.parent.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(DB)
    con.execute("PRAGMA journal_mode=WAL")
    con.executescript(DDL)
    print("cvelistV5", flush=True)
    load_cvelist(con, years)
    print("vulnrichment", flush=True)
    load_vulnrichment(con, years)
    load_nvd(con, years)
    load_kev(con)
    load_epss(con)
    load_cwe(con)
    for name, fn in (("exploitdb", load_exploitdb), ("nuclei", load_nuclei),
                     ("metasploit", load_metasploit), ("poc_github", load_poc_github),
                     ("ghsa", load_ghsa), ("redhat", load_redhat), ("debian", load_debian)):
        print(name, flush=True)
        fn(con)
    con.execute("CREATE INDEX idx_cwe_cve ON cwe_assignment(cve_id)")
    con.execute("CREATE INDEX idx_cvss_cve ON cvss(cve_id)")
    con.execute("CREATE INDEX idx_poc_cve ON poc(cve_id)")
    con.execute("CREATE INDEX idx_advisory_cve ON advisory(cve_id)")
    con.commit()
    for table in ("cve", "cwe_assignment", "cvss", "kev", "epss", "nvd_status",
                  "cwe_hierarchy", "poc", "advisory"):
        print(f"{table}: {con.execute(f'SELECT COUNT(*) FROM {table}').fetchone()[0]} rows")
    print("poc by source:", dict(con.execute("SELECT source, COUNT(*) FROM poc GROUP BY 1")))
    con.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--years", type=int, nargs="*", help="restrict to these CVE years")
    run(ap.parse_args())
