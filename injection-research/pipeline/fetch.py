"""Stage 1: acquire pinned snapshots (cvelistV5, NVD, Vulnrichment, KEV, EPSS, CWE).

Existing files are never re-downloaded; their hashes are verified against
manifest.json, which records every pin needed to reproduce the corpus.
"""
import argparse
import datetime
import json
import subprocess
import urllib.request
from pathlib import Path

import snapshots

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
    # PoC linkage and exploit-availability scoring
    "trickest-cve": "https://github.com/trickest/cve.git",
    "cve-scores": "https://github.com/ARPSyndicate/cve-scores.git",
    "vulhub": "https://github.com/vulhub/vulhub.git",
    # Payload corpora: the attack strings themselves, per sink type. These drive
    # the matchertext-prevention measurement, which needs payloads rather than
    # CVE metadata.
    "PayloadsAllTheThings": "https://github.com/swisskyrepo/PayloadsAllTheThings.git",
    "fuzzdb": "https://github.com/fuzzdb-project/fuzzdb.git",
    "SecLists": "https://github.com/danielmiessler/SecLists.git",
    # Labeled ground truth: injection cases with known CWE and known verdict.
    "BenchmarkJava": "https://github.com/OWASP-Benchmark/BenchmarkJava.git",
    # Additional public CVE PoC collections and link indexes. Repositories with
    # no open-source license remain research inputs; their terms are recorded in
    # the manifest and their files stay below data/raw, which is not exported.
    "awesome-poc": "https://github.com/Threekiii/Awesome-POC.git",
    "wy876-poc": "https://github.com/wy876/POC.git",
    "penetration-testing-poc": "https://github.com/Mr-xn/Penetration_Testing_POC.git",
    "some-poc-or-exp": "https://github.com/coffeehb/Some-PoC-oR-ExP.git",
    "peiqi-wiki": "https://github.com/PeiQi0/PeiQi-WIKI-Book.git",
    "poc-lab": "https://github.com/Unclecheng-li/poc-lab.git",
    "xray": "https://github.com/chaitin/xray.git",
    "0xmarcio-cve": "https://github.com/0xMarcio/cve.git",
    "zulloper-cve-poc": "https://github.com/zulloper/cve-poc.git",
}
SOURCE_METADATA = {
    "awesome-poc": {"license": "NOASSERTION", "use": "research-only"},
    "wy876-poc": {"license": "NOASSERTION", "use": "repository terms apply"},
    "penetration-testing-poc": {"license": "Apache-2.0"},
    "some-poc-or-exp": {"license": "NOASSERTION", "use": "research-only"},
    "peiqi-wiki": {"license": "NOASSERTION", "use": "authorized research only"},
    "poc-lab": {"license": "MIT"},
    "xray": {"license": "LicenseRef-xray",
             "use": "attribution and disclaimer acceptance required"},
    "0xmarcio-cve": {"license": "MIT"},
    "zulloper-cve-poc": {"license": "NOASSERTION"},
}
REDHAT_URL = "https://access.redhat.com/hydra/rest/securitydata/cve.json"
DEBIAN_URL = "https://security-tracker.debian.org/tracker/data/json"
# NIST SARD Juliet suites: synthetic cases whose paths encode the CWE, giving a
# CWE-labeled corpus independent of the CVE record.
SARD_SUITES = {
    "juliet-java": "https://samate.nist.gov/SARD/downloads/test-suites/"
                   "2017-10-01-juliet-test-suite-for-java-v1-3.zip",
    "juliet-c": "https://samate.nist.gov/SARD/downloads/test-suites/"
                "2017-10-01-juliet-test-suite-for-c-cplusplus-v1-3.zip",
}
REFRESH = False


def sha256(path):
    return snapshots.sha256(path)


def download(url, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    req = urllib.request.Request(url, headers={"User-Agent": "matchertext-injection-research"})
    with urllib.request.urlopen(req) as r, open(tmp, "wb") as f:
        while chunk := r.read(1 << 20):
            f.write(chunk)
    tmp.rename(dest)


def pin(manifest, key, url, dest, today, fetcher=download, **extra):
    entry, expected = manifest.get(key, {}), manifest.get(key, {}).get("sha256")
    if expected and not REFRESH:
        if not dest.exists() or sha256(dest) != expected:
            if not snapshots.materialize(expected, dest):
                print(f"fetching pinned {key} ...", flush=True)
                fetcher(url, dest)
                actual = sha256(dest)
                if actual != expected:
                    dest.unlink()
                    raise RuntimeError(
                        f"{key}: upstream no longer serves pinned object {expected}; "
                        "configure MATCHERTEXT_SNAPSHOT_ARCHIVE")
        snapshots.store(dest, expected)
        return
    print(f"refreshing {key} ..." if expected else f"fetching {key} ...", flush=True)
    fetcher(url, dest)
    digest = snapshots.store(dest)
    manifest[key] = {**entry, "url": url, "sha256": digest,
                     "fetched": str(today), **extra}


def git_head(dest):
    return subprocess.run(["git", "-C", str(dest), "rev-parse", "HEAD"],
                          capture_output=True, text=True, check=True).stdout.strip()


def git_fetch(dest, url, commit=None):
    if not dest.exists():
        dest.mkdir(parents=True)
        subprocess.run(["git", "-C", str(dest), "init"], check=True,
                       stdout=subprocess.DEVNULL)
        subprocess.run(["git", "-C", str(dest), "remote", "add", "origin", url],
                       check=True)
    target = commit or "HEAD"
    subprocess.run(["git", "-C", str(dest), "fetch", "--depth", "1", "origin", target],
                   check=True)
    subprocess.run(["git", "-C", str(dest), "checkout", "--detach", "--force",
                    "FETCH_HEAD"], check=True, stdout=subprocess.DEVNULL)
    head = git_head(dest)
    if commit and head != commit:
        raise RuntimeError(f"{dest.name}: expected {commit}, got {head}")
    return head


def run(args):
    global REFRESH
    REFRESH = getattr(args, "refresh", False)
    manifest = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {}
    today = datetime.datetime.now(datetime.UTC).date()

    for name, url in GIT_REPOS.items():
        dest = RAW / name
        entry = manifest.get(name, {})
        pinned = entry.get("commit")
        if entry.get("status") == "unavailable" and not REFRESH:
            print(f"{name}: pinned as unavailable, skipping", flush=True)
            continue
        try:
            head = git_head(dest) if dest.exists() else None
            if REFRESH or not pinned:
                print(f"refreshing {name} ...", flush=True)
                head = git_fetch(dest, url)
                entry = {**entry, "url": url, "commit": head, "fetched": str(today),
                         **SOURCE_METADATA.get(name, {})}
                entry.pop("status", None)
                manifest[name] = entry
            elif head != pinned:
                print(f"fetching pinned {name} ...", flush=True)
                head = git_fetch(dest, url, pinned)
        except subprocess.CalledProcessError:
            if name not in SOURCE_METADATA or pinned:
                raise
            manifest[name] = {"url": url, "fetched": str(today),
                              "status": "unavailable", **SOURCE_METADATA.get(name, {})}
            print(f"{name}: unavailable, skipping", flush=True)
            continue
        print(f"{name}: {head}")

    years = getattr(args, "years", None)
    pinned_years = sorted(int(key[4:]) for key in manifest if key.startswith("nvd-"))
    available_years = range(2002, today.year + 1) if REFRESH or not pinned_years else pinned_years
    for year in available_years:
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

    for name, url in SARD_SUITES.items():
        pin(manifest, name, url, RAW / "sard" / f"{name}.zip", today)

    pin(manifest, "redhat", REDHAT_URL, RAW / "redhat" / "redhat_cve.json", today,
        fetcher=fetch_redhat)
    pin(manifest, "debian", DEBIAN_URL, RAW / "debian" / "debian_security.json", today)

    MANIFEST.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(f"manifest written: {MANIFEST}")


def fetch_redhat(url, dest):
    """Paginate the Red Hat security-data list endpoint into one JSON array."""
    rows, page = [], 1
    while True:
        req = urllib.request.Request(f"{url}?per_page=1000&page={page}",
                                     headers={"User-Agent": "matchertext-injection-research"})
        with urllib.request.urlopen(req) as response:
            batch = json.load(response)
        if not batch:
            break
        rows.extend(batch)
        page += 1
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(rows))
    print(f"redhat: {len(rows)} records", flush=True)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--years", type=int, nargs="*", help="restrict NVD feeds to these years")
    ap.add_argument("--epss-date", help="EPSS snapshot date YYYY-MM-DD (default: yesterday)")
    ap.add_argument("--refresh", action="store_true",
                    help="replace manifest pins with current upstream revisions")
    ap.add_argument("--freeze", action="store_true", help=argparse.SUPPRESS)
    run(ap.parse_args())
