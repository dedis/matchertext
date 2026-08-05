"""Run the full CVE injection-classification pipeline (stages 1-4).

Usage:
  python3 pipeline/run_all.py                      # full corpus
  python3 pipeline/run_all.py --years 2024         # single-year slice
  python3 pipeline/run_all.py --skip-fetch         # rebuild from pinned snapshots
  python3 pipeline/run_all.py --freeze             # keep existing rolling feeds pinned
"""
import argparse

import build_db
import classify
import export
import fetch
import latex
import sqli_experiment
import subclass
import syntactic_group
import validate


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--years", type=int, nargs="*", help="restrict to these CVE years")
    ap.add_argument("--epss-date", help="EPSS snapshot date YYYY-MM-DD (default: yesterday)")
    ap.add_argument("--skip-fetch", action="store_true")
    ap.add_argument("--freeze", action="store_true",
                    help="keep existing rolling-source snapshots pinned")
    ap.add_argument("--nb-margin", type=float, default=3.0,
                    help="minimum NB log-odds margin to accept a label")
    ap.add_argument("--skip-sabotage", action="store_true",
                    help="skip the driver experiment's power measurement")
    ap.add_argument("--sabotage-n", type=int, default=4000)
    args = ap.parse_args()
    if not args.skip_fetch:
        fetch.run(args)
    build_db.run(args)
    classify.run(args)
    subclass.run(args)
    syntactic_group.run(args)
    validate.run(args)
    sqli_experiment.run(args)
    export.run(args)
    latex.run(args)


if __name__ == "__main__":
    main()
