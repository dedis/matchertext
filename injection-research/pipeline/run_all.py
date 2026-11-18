"""Run the full pipeline: fetch, build_db, classify, syntactic_group, subclass, validate, the SQL and HTML
replays, export, report, and latex (README, "Reproduce the complete pipeline").

Usage:
  python3 pipeline/run_all.py                      # full corpus
  python3 pipeline/run_all.py --years 2024         # single-year slice
  python3 pipeline/run_all.py --skip-fetch         # rebuild from pinned snapshots
  python3 pipeline/run_all.py --refresh            # update all snapshot pins
"""
import argparse
import json
import os
import platform
import subprocess

import build_db
import classify
import export
import fetch
import html_experiment
import latex
import report
import sqli_experiment
import subclass
import syntactic_group
import validate


def pin_toolchain():
    """Record in manifest.json the fork commits and the tool versions of this
    run, beside the input pins."""
    def first_line(*cmd, cwd=fetch.ROOT, stdin=None):
        return subprocess.run(cmd, cwd=cwd, input=stdin, capture_output=True, text=True,
                              check=True).stdout.strip().splitlines()[0]

    def commit(path):
        """The commit of a fork; "-dirty" when its tracked files differ from it, so the record is not taken for
        the code that ran."""
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=path,
                               capture_output=True, text=True, check=True).stdout.strip()
        return first_line("git", "rev-parse", "HEAD", cwd=path) + ("-dirty" if dirty else "")

    manifest = json.loads(fetch.MANIFEST.read_text())
    manifest["toolchain"] = {
        "sqlite-fork": commit(fetch.ROOT / "sqlite"),
        "parse5-fork": commit(fetch.ROOT / "parse5"),
        "lean": (fetch.ROOT.parent / "lean" / "lean-toolchain").read_text().strip(),
        "python": platform.python_version(),
        "node": first_line("node", "--version"),
        "cc": first_line(os.environ.get("CC", "cc"), "--version"),
        "tcl": first_line("tclsh", stdin="puts [info patchlevel]\n"),
    }
    fetch.MANIFEST.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--years", type=int, nargs="*", help="restrict to these CVE years")
    ap.add_argument("--epss-date", help="EPSS snapshot date YYYY-MM-DD (default: yesterday)")
    ap.add_argument("--skip-fetch", action="store_true")
    ap.add_argument("--refresh", action="store_true",
                    help="replace manifest pins with current upstream revisions")
    ap.add_argument("--freeze", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--nb-margin", type=float, default=3.0,
                    help="minimum NB log-odds margin to accept a label")
    ap.add_argument("--skip-sabotage", action="store_true",
                    help="skip the driver experiments' power measurement")
    ap.add_argument("--sabotage-n", type=int, help="sabotage only the first N payloads")
    args = ap.parse_args()
    pin_toolchain()
    if not args.skip_fetch:
        fetch.run(args)
    build_db.run(args)
    classify.run(args)
    syntactic_group.run(args)
    subclass.run(args)
    validate.run(args)
    sqli_experiment.run(args)
    html_experiment.run(args)
    export.run(args)
    report.run(argparse.Namespace(samples=5, seed=42))
    latex.run(args)


if __name__ == "__main__":
    main()
