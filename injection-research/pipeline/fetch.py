"""Stage 1: acquire pinned snapshots (cvelistV5, NVD, Vulnrichment, KEV, EPSS, CWE).

Existing files are never re-downloaded; their hashes are verified against
manifest.json, which records every pin needed to reproduce the corpus.
"""
import argparse
import datetime
import hashlib
import json
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "data" / "raw"
MANIFEST = ROOT / "manifest.json"

NVD_FEED = "https://nvd.nist.gov/feeds/json/cve/2.0/nvdcve-2.0-{year}.json.gz"
KEV_URL = "https://raw.githubusercontent.com/cisagov/kev-data/develop/known_exploited_vulnerabilities.json"
EPSS_URL = "https://epss.empiricalsecurity.com/epss_scores-{date}.csv.gz"
CWE_URL = "https://cwe.mitre.org/data/xml/cwec_latest.xml.zip"
GIT_REPOS = {
    "cvelistV5": "https://github.com/CVEProject/cvelistV5.git",
    "vulnrichment": "https://github.com/cisagov/vulnrichment.git",
    "exploitdb": "https://gitlab.com/exploit-database/exploitdb.git",
    "nuclei-templates": "https://github.com/projectdiscovery/nuclei-templates.git",
    "metasploit-framework": "https://github.com/rapid7/metasploit-framework.git",
    "advisory-database": "https://github.com/github/advisory-database.git",
    "PoC-in-GitHub": "https://github.com/nomi-sec/PoC-in-GitHub.git",
}
REDHAT_URL = "https://access.redhat.com/hydra/rest/securitydata/cve.json"
DEBIAN_URL = "https://security-tracker.debian.org/tracker/data/json"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    req = urllib.request.Request(url, headers={"User-Agent": "matchertext-injection-research"})
    with urllib.request.urlopen(req) as r, open(tmp, "wb") as f:
        while chunk := r.read(1 << 20):
            f.write(chunk)
    tmp.rename(dest)


def pin(manifest, key, url, dest, today, **extra):
    if not dest.exists():
        print(f"fetching {key} ...", flush=True)
        download(url, dest)
    digest = sha256(dest)
    prev = manifest.get(key, {}).get("sha256")
    if prev and prev != digest:
        sys.exit(f"{key}: {dest} does not match pinned sha256 in {MANIFEST}")
    manifest[key] = {"url": url, "sha256": digest, "fetched": manifest.get(key, {}).get("fetched", str(today)), **extra}


def run(args):
    manifest = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {}
    today = datetime.datetime.now(datetime.UTC).date()

    for name, url in GIT_REPOS.items():
        dest = RAW / name
        if not dest.exists():
            print(f"cloning {name} ...", flush=True)
            subprocess.run(["git", "clone", "--depth", "1", url, str(dest)], check=True)
        head = subprocess.run(["git", "-C", str(dest), "rev-parse", "HEAD"],
                              capture_output=True, text=True, check=True).stdout.strip()
        manifest[name] = {"url": url, "commit": head,
                          "fetched": manifest.get(name, {}).get("fetched", str(today))}
        print(f"{name}: {head}")

    years = getattr(args, "years", None)
    for year in range(2002, today.year + 1):
        if years and year not in years:
            continue
        pin(manifest, f"nvd-{year}", NVD_FEED.format(year=year),
            RAW / "nvd" / f"nvdcve-2.0-{year}.json.gz", today)

    pin(manifest, "kev", KEV_URL, RAW / "kev.json", today)

    date = getattr(args, "epss_date", None) or manifest.get("epss", {}).get("date") \
        or str(today - datetime.timedelta(days=1))
    pin(manifest, "epss", EPSS_URL.format(date=date),
        RAW / "epss" / f"epss_scores-{date}.csv.gz", today, date=date)

    pin(manifest, "cwe", CWE_URL, RAW / "cwe" / "cwec_latest.xml.zip", today)

    fetch_redhat(manifest, today)
    pin(manifest, "debian", DEBIAN_URL, RAW / "debian" / "debian_security.json", today)

    MANIFEST.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(f"manifest written: {MANIFEST}")


def fetch_redhat(manifest, today):
    """Paginate the Red Hat security-data list endpoint into one JSON array."""
    dest = RAW / "redhat" / "redhat_cve.json"
    if not dest.exists():
        print("fetching redhat ...", flush=True)
        rows, page = [], 1
        while True:
            url = f"{REDHAT_URL}?per_page=1000&page={page}"
            req = urllib.request.Request(url, headers={"User-Agent": "matchertext-injection-research"})
            with urllib.request.urlopen(req) as r:
                batch = json.load(r)
            if not batch:
                break
            rows.extend(batch)
            page += 1
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(json.dumps(rows))
        print(f"redhat: {len(rows)} records", flush=True)
    digest = sha256(dest)
    manifest["redhat"] = {"url": REDHAT_URL, "sha256": digest,
                          "fetched": manifest.get("redhat", {}).get("fetched", str(today))}


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--years", type=int, nargs="*", help="restrict NVD feeds to these years")
    ap.add_argument("--epss-date", help="EPSS snapshot date YYYY-MM-DD (default: yesterday)")
    run(ap.parse_args())
