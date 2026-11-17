"""SQLite's own test suite on the stock and the additive matchertext build of
the fork's tree: the additive build must leave stock SQL working. Builds each
testfixture out of tree and runs test/testrunner.tcl with the PERMUTATION
test set. Runs apart from run_all.py, like perf.py; latex.py quotes
data/exports/compat.csv when it exists.

compat.csv: one row per build, build, tests, errors, and the failed test
cases, written only when the additive build fails no case that the stock
build passes.

Set LDFLAGS when the Tcl library needs a search path (Homebrew Tcl 9 on
macOS: LDFLAGS=-L/opt/homebrew/lib).
"""
import os
import re
import subprocess
import sys

from export import write
from sqli_experiment import BUILD, SQLITE_TREE

PERMUTATION = "full"
BUILDS = {"stock": "", "additive": "-DSQLITE_ENABLE_MATCHERTEXT"}


def run(cmd, what, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"{what} failed (exit {r.returncode}):\n{r.stdout[-2000:]}{r.stderr[-2000:]}")


def suite(name, options):
    """(tests, errors, failed cases) of the suite on one build."""
    d = BUILD / f"sqlite-testfixture-{name}"
    d.mkdir(parents=True, exist_ok=True)
    if not (d / "Makefile").exists():
        run([str(SQLITE_TREE / "configure")], f"configure ({name})", d)
    ld = [f"LDFLAGS.configure={os.environ['LDFLAGS']}"] if os.environ.get("LDFLAGS") else []
    run(["make", f"-j{os.cpu_count()}", f"OPTIONS={options}", *ld, "testfixture"],
        f"testfixture ({name})", d)
    jobs = max(1, (os.cpu_count() or 2) // 2)
    r = subprocess.run([str(d / "testfixture"), str(SQLITE_TREE / "test" / "testrunner.tcl"),
                        "--jobs", str(jobs), PERMUTATION], cwd=d, capture_output=True, text=True)
    m = re.search(r"(\d+) errors out of (\d+) tests", r.stdout)
    if m is None:
        sys.exit(f"testrunner ({name}) gave no summary (exit {r.returncode}):\n{r.stdout[-2000:]}")
    errors, tests = int(m.group(1)), int(m.group(2))
    r = subprocess.run([str(d / "testfixture"), str(SQLITE_TREE / "test" / "testrunner.tcl"), "errors"],
                       cwd=d, capture_output=True, text=True)
    failed = sorted({case for line in r.stdout.splitlines()
                     if line.startswith("!Failures on these tests:")
                     for case in line.split(":", 1)[1].split()})
    print(f"  {name:9} {tests:>9,} tests  {errors:>4,} errors  {' '.join(failed)}")
    return tests, errors, " ".join(failed)


def main():
    rows = [(name, *suite(name, options)) for name, options in BUILDS.items()]
    new = set(rows[1][3].split()) - set(rows[0][3].split())
    if new:
        sys.exit(f"the additive build fails cases the stock build passes: {' '.join(sorted(new))}")
    write("compat.csv", ["build", "tests", "errors", "failed"], rows)


if __name__ == "__main__":
    main()
