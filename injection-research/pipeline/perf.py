"""What the two forks cost in time. Timings depend on the machine, so this runs
apart from run_all.py; latex.py quotes data/exports/perf.csv when it exists.

  speedtest1   SQLite's own benchmark (main test set, in memory), the stock
               build against the additive build of the same tree
  prepare_*    one value into a prepared statement, by length (prepare_bench.c):
               bind (the baseline), the %m hole, and the typed ?V path
  parse5_*     parse and serialize of parse5's huge benchmark page, the
               upstream commit the fork starts from against the fork (perf.mjs)

perf.csv: one row per case, case, baseline, fork (milliseconds, or nanoseconds
for prepare_*), and fork / baseline.
"""
import json
import os
import re
import shutil
import statistics
import subprocess
import sys

import html_experiment
import sqli_experiment
from export import write

ROOT = sqli_experiment.ROOT
BUILD = sqli_experiment.BUILD
PARSE5_BASE = "476b24b01538a5e8fd5cb74bf8e1433b3dc7e157"   # upstream parse5 under the fork
PARSE5_STOCK = BUILD / "parse5-stock"
RUNS = 7
PARSE5_PROCESSES = 5        # one node process times each case with one JIT state


def run(cmd, what, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"{what} failed (exit {r.returncode}):\n{r.stderr[-4000:]}")
    return r.stdout


def build_speedtest():
    """speedtest1 against the stock amalgamation and the additive one."""
    sqli_experiment.build_amalgamation()
    sqli_experiment.build_stock_amalgamation()
    cc = os.environ.get("CC", "cc")
    out = {}
    for name, amalg, extra in (("stock", sqli_experiment.STOCK_AMALG, []),
                               ("additive", sqli_experiment.AMALG, ["-DSQLITE_ENABLE_MATCHERTEXT"])):
        out[name] = BUILD / f"speedtest1-{name}"
        run([cc, "-O2", "-o", str(out[name]), str(sqli_experiment.SQLITE_TREE / "test" / "speedtest1.c"),
             str(amalg), "-I", str(amalg.parent), *extra, "-lm", "-lpthread"], f"speedtest1 ({name})")
    return out


def speedtest(binaries):
    """Median TOTAL time, in milliseconds, of RUNS alternating runs per build."""
    times = {name: [] for name in binaries}
    for _ in range(RUNS):
        for name, binary in binaries.items():
            text = run([str(binary), "--size", "100", "--testset", "main", "--memdb"], "speedtest1")
            times[name].append(float(re.search(r"TOTAL\.*\s+([\d.]+)s", text).group(1)))
    return statistics.median(times["stock"]) * 1e3, statistics.median(times["additive"]) * 1e3


def prepare_bench():
    binary = BUILD / "prepare_bench"
    cc = os.environ.get("CC", "cc")
    run([cc, "-O2", "-o", str(binary), str(ROOT / "test" / "matchertext" / "csrc" / "prepare_bench.c"),
         str(sqli_experiment.AMALG), "-I", str(sqli_experiment.AMALG.parent),
         "-DSQLITE_ENABLE_MATCHERTEXT", "-DSQLITE_OMIT_LOAD_EXTENSION", "-lm", "-lpthread"],
        "prepare_bench")
    ns = {}
    for line in run([str(binary)], "prepare_bench").splitlines():
        length, path, value = line.split()
        ns[(int(length), path)] = float(value)
    return ns


def build_parse5_stock():
    """The upstream parse5 that the fork starts from, compiled with the fork's TypeScript."""
    shutil.rmtree(PARSE5_STOCK, ignore_errors=True)
    PARSE5_STOCK.mkdir(parents=True)
    tar = subprocess.run(["git", "-C", str(html_experiment.FORK), "archive", PARSE5_BASE,
                          "tsconfig.json", "packages/parse5"], capture_output=True, check=True).stdout
    subprocess.run(["tar", "-x", "-C", str(PARSE5_STOCK)], input=tar, check=True)
    os.symlink(html_experiment.FORK / "node_modules", PARSE5_STOCK / "node_modules")
    run(["node", str(html_experiment.FORK / "node_modules" / "typescript" / "bin" / "tsc"), "-p",
         str(PARSE5_STOCK / "packages" / "parse5" / "tsconfig.json")], "tsc (stock parse5)")
    return PARSE5_STOCK / "packages" / "parse5" / "dist" / "index.js"


def parse5_bench(stock):
    """Per case, the median stock and fork times over PARSE5_PROCESSES runs of perf.mjs."""
    html_experiment.build_fork()
    times = {}
    for _ in range(PARSE5_PROCESSES):
        out = run(["node", str(ROOT / "test" / "matchertext" / "jssrc" / "perf.mjs"), str(stock),
                   str(html_experiment.DIST / "index.js"),
                   str(html_experiment.FORK / "test" / "data" / "huge-page" / "huge-page.html")], "perf.mjs")
        for r in map(json.loads, out.splitlines()):
            times.setdefault(r["case"], []).append((r["stock_ms"], r["fork_ms"]))
    return [{"case": case, "stock_ms": statistics.median(s for s, _ in t),
             "fork_ms": statistics.median(f for _, f in t)} for case, t in times.items()]


def main():
    rows = [("speedtest1", *speedtest(build_speedtest()))]
    ns = prepare_bench()
    for length in sorted({k[0] for k in ns}):
        for path in ("mt", "typed"):
            rows.append((f"prepare_{path}_{length}", ns[(length, "bind")], ns[(length, path)]))
    for r in parse5_bench(build_parse5_stock()):
        rows.append((f"parse5_{r['case']}", r["stock_ms"], r["fork_ms"]))
    rows = [(case, round(b, 3), round(f, 3), round(f / b, 3)) for case, b, f in rows]
    for row in rows:
        print("  {:32} {:>12} {:>12} {:>7}".format(*row))
    write("perf.csv", ["case", "baseline", "fork", "ratio"], rows)


if __name__ == "__main__":
    main()
