#!/usr/bin/env python3
"""Run the full corpus analysis: clone and parse every organisation, then
build the global aggregate.

Usage:
  ./run-analysis.py [--build] [--fresh] [--attempts N] [<org> ...]

With no org names, all organisations in ORGS are analysed.

Steps:
  1. Build the parser (`make train build`) if it is missing or --build is set.
  2. With --fresh, move result/repos/ to result/repos.<timestamp>/ so that no
     old per-language files stay in the new results.
  3. Run clone-orgs.sh once per organisation. If an organisation fails, run
     clone-orgs.sh for it again, up to --attempts times. If a failed attempt
     stopped before clone-orgs.sh deleted the clone tree, repositories already
     on disk are not downloaded again.
  4. Run build_global.py, which writes result/global/.

Tunables of clone-orgs.sh (JOBS, DEPTH, SKIP_ARCHIVED, SKIP_FORKS, RETRIES)
pass through from the environment. RETRIES is the number of tries for each
repository clone (default 3). OUTPUT is always ./repos, because
build_global.py reads result/repos/.
"""
import argparse, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
REPOS = os.path.join("result", "repos")

# The 79 GitHub organisations of the corpus.
ORGS = [
    "Alamofire", "Azure", "Bioconductor", "Debian", "EpicGames", "Homebrew",
    "JetBrains", "JuliaLang", "NixOS", "Shopify", "WebAssembly", "WordPress",
    "airbnb", "angular", "apache", "apple", "archlinux", "aws", "chromium",
    "clojure", "cockroachdb", "coreutils", "curl", "dart-lang", "dask",
    "discourse", "docker", "dotnet", "elastic", "elixir-lang", "facebook",
    "flutter", "freebsd", "gentoo", "godotengine", "golang", "gradle", "grpc",
    "haskell", "huggingface", "jupyter", "keras-team", "kubernetes", "laravel",
    "llvm", "matplotlib", "mongodb", "mozilla", "nodejs", "numpy", "ocaml",
    "openjdk", "openssl", "pandas-dev", "php", "pointfreeco", "postgres",
    "prometheus", "purescript", "python", "pytorch", "rails", "redis",
    "rstudio", "rust-lang", "scikit-learn", "scipy", "square", "sveltejs",
    "swiftlang", "symfony", "systemd", "tidyverse", "torproject", "torvalds",
    "v8", "vercel", "vitejs", "vuejs",
]


def log(msg):
    print(msg, file=sys.stderr, flush=True)


def aggregate(org):
    return os.path.join(REPOS, org, org, "strings.md")


def run_org(org, attempts, env):
    """Run clone-orgs.sh for one org until it writes a new aggregate.

    Returns True when the org was parsed. clone-orgs.sh exits non-zero when
    some repositories fail to clone, even if the org was parsed, so success
    is a new result/repos/<org>/<org>/strings.md, not the exit code.
    """
    for attempt in range(1, attempts + 1):
        start = time.time()
        code = subprocess.run(["./clone-orgs.sh", org], env=env).returncode
        if os.path.isfile(aggregate(org)) and os.path.getmtime(aggregate(org)) >= start:
            if code != 0:
                log("%s: parsed, but some repositories did not clone (see repos/clone_failures.log)" % org)
            return True
        log("%s: attempt %d of %d failed, no new %s" % (org, attempt, attempts, aggregate(org)))
        if attempt < attempts:
            wait = 30 * attempt
            log("%s: trying again in %d s" % (org, wait))
            time.sleep(wait)
    return False


def main():
    ap = argparse.ArgumentParser(description="Clone and parse every organisation, then run build_global.py.")
    ap.add_argument("orgs", nargs="*", help="organisations (default: all %d in ORGS)" % len(ORGS))
    ap.add_argument("--build", action="store_true", help="rebuild the parser first")
    ap.add_argument("--fresh", action="store_true",
                    help="move result/repos/ aside before the run (nothing is deleted)")
    ap.add_argument("--attempts", type=int, default=3,
                    help="runs of clone-orgs.sh per organisation before it counts as failed (default 3)")
    args = ap.parse_args()

    os.chdir(HERE)
    orgs = args.orgs or ORGS

    if args.build or not os.access("parser", os.X_OK):
        log("building the parser: make train build")
        subprocess.run(["make", "train", "build"], check=True)

    if args.fresh and os.path.isdir(REPOS):
        old = "%s.%s" % (REPOS, time.strftime("%Y%m%d-%H%M%S"))
        log("moving %s to %s" % (REPOS, old))
        os.rename(REPOS, old)

    env = dict(os.environ, OUTPUT="./repos")
    failed = []
    for i, org in enumerate(orgs, 1):
        log("\n[%d/%d] %s" % (i, len(orgs), org))
        if not run_org(org, max(1, args.attempts), env):
            failed.append(org)

    log("\nbuilding the global aggregate: build_global.py")
    subprocess.run([sys.executable, "build_global.py"], check=True)

    log("\ndone: %d of %d organisations parsed" % (len(orgs) - len(failed), len(orgs)))
    if failed:
        log("failed after %d attempts: %s" % (args.attempts, " ".join(failed)))
        sys.exit(1)


if __name__ == "__main__":
    main()
