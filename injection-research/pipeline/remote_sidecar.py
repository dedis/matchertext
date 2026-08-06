"""Import and export pinned GitHub payload-recovery results."""
import argparse
import json
import sqlite3
from pathlib import Path

import snapshots

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "manifest.json"
PINS = ROOT / "data" / "github_pins.json"
KEY = "remote-payloads"
REMOTE_CREATE_DDL = """
CREATE TABLE IF NOT EXISTS remote_payload(
    cve_id TEXT PRIMARY KEY, source TEXT, repo TEXT, sha TEXT,
    file TEXT, payload TEXT, status TEXT);
CREATE TABLE IF NOT EXISTS remote_attempt(
    cve_id TEXT, repo TEXT, sha TEXT, file TEXT, payload TEXT, status TEXT,
    PRIMARY KEY(cve_id, repo));
"""
WEB_CREATE_DDL = """
CREATE TABLE IF NOT EXISTS web_payload(
    cve_id TEXT PRIMARY KEY, source TEXT, url TEXT, sha256 TEXT,
    payload TEXT, status TEXT);
CREATE TABLE IF NOT EXISTS ghsl_page(
    url TEXT PRIMARY KEY, sha256 TEXT, status TEXT);
"""
CREATE_DDL = REMOTE_CREATE_DDL + WEB_CREATE_DDL
DDL = """DROP TABLE IF EXISTS remote_payload;
DROP TABLE IF EXISTS remote_attempt;
DROP TABLE IF EXISTS web_payload;
DROP TABLE IF EXISTS ghsl_page;
""" + CREATE_DDL


def _rows(con, table, columns):
    return con.execute(f"SELECT {','.join(columns)} FROM {table} "
                       f"ORDER BY {','.join(columns)}").fetchall()


def export(con, pins_path=PINS):
    """Write a canonical sidecar and pin its content hash in the manifest."""
    payload = _rows(con, "remote_payload",
                    ("cve_id", "source", "repo", "sha", "file", "payload", "status"))
    attempts = _rows(con, "remote_attempt",
                     ("cve_id", "repo", "sha", "file", "payload", "status"))
    web = _rows(con, "web_payload",
                ("cve_id", "source", "url", "sha256", "payload", "status"))
    pages = _rows(con, "ghsl_page", ("url", "sha256", "status"))
    pins = json.loads(Path(pins_path).read_text()) if Path(pins_path).exists() else {}
    data = json.dumps({"schema": 2, "remote_payload": payload,
                       "remote_attempt": attempts, "web_payload": web,
                       "ghsl_page": pages, "github_pins": pins},
                      ensure_ascii=False, separators=(",", ":"),
                      sort_keys=True).encode()
    archive = snapshots.local_archive()
    if archive is None:
        raise RuntimeError("sidecar export requires a local snapshot archive")
    sidecar = archive / "remote-payloads.json"
    sidecar.parent.mkdir(parents=True, exist_ok=True)
    tmp = sidecar.with_suffix(".part")
    tmp.write_bytes(data)
    tmp.replace(sidecar)
    digest = snapshots.store(sidecar)
    manifest = json.loads(MANIFEST.read_text())
    manifest[KEY] = {"format": "matchertext-derived-payloads-v2",
                     "sha256": digest, "remote_payload": len(payload),
                     "remote_attempt": len(attempts), "web_payload": len(web),
                     "ghsl_page": len(pages)}
    MANIFEST.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return digest


def import_into(con):
    """Replace database recovery tables with the exact pinned sidecar."""
    con.executescript(DDL)
    manifest = json.loads(MANIFEST.read_text())
    entry = manifest.get(KEY)
    if not entry:
        con.commit()
        return 0
    document = json.loads(snapshots.read(entry["sha256"]))
    if document.get("schema") != 2:
        raise RuntimeError("unsupported remote-payload sidecar schema")
    payload = document["remote_payload"]
    attempts = document["remote_attempt"]
    web = document["web_payload"]
    pages = document["ghsl_page"]
    if any(len(rows) != entry[name] for name, rows in (
            ("remote_payload", payload), ("remote_attempt", attempts),
            ("web_payload", web), ("ghsl_page", pages))):
        raise RuntimeError("remote-payload sidecar row count mismatch")
    con.executemany("INSERT INTO remote_payload VALUES(?,?,?,?,?,?,?)", payload)
    con.executemany("INSERT INTO remote_attempt VALUES(?,?,?,?,?,?)", attempts)
    con.executemany("INSERT INTO web_payload VALUES(?,?,?,?,?,?)", web)
    con.executemany("INSERT INTO ghsl_page VALUES(?,?,?)", pages)
    PINS.parent.mkdir(parents=True, exist_ok=True)
    PINS.write_text(json.dumps(document.get("github_pins", {}), indent=1, sort_keys=True))
    con.commit()
    return len(payload)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--db", type=Path, default=ROOT / "data" / "cve.db")
    ap.add_argument("--pins", type=Path, default=PINS)
    args = ap.parse_args()
    db = sqlite3.connect(args.db)
    print(export(db, args.pins))
    db.close()
