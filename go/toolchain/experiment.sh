#!/usr/bin/env bash
#
# experiment.sh — test matchertext literals in Go on real repositories.
#
# Usage: ./experiment.sh <work-dir> <repo-url>[@<commit>] ...
#
# Builds the patched toolchain (build.sh) and runs its acceptance tests. Then,
# for each repository:
#   1. go test with stock Go                     (baseline)
#   2. go test with patched Go, code unchanged   (the extension is conservative)
#   3. rewrite string literals as M"(...)" (mrewrite), go test with patched Go
# and compares the package outcomes of the three runs. go test also runs its
# default vet checks. Results go to <work-dir>/results.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
work=${1:?usage: experiment.sh <work-dir> <repo-url>[@<commit>] ...}
shift
mkdir -p "$work"
work=$(cd "$work" && pwd)
results=$work/results
mkdir -p "$results" "$work/repos"

stock_root=$(go env GOROOT)
patched_root=$work/go
"$here/build.sh" "$work" > "$results/build.log" 2>&1

# go with the stock or patched toolchain. An inherited GOROOT would select the
# stock toolchain for both, so it is always set.
gostock() { GOROOT=$stock_root GOCACHE=$work/cache-stock GOTOOLCHAIN=local "$stock_root/bin/go" "$@"; }
gopatched() { GOROOT=$patched_root PATH=$patched_root/bin:$PATH GOCACHE=$work/cache-patched GOTOOLCHAIN=local "$patched_root/bin/go" "$@"; }

(cd "$here/../.." && gostock build -o "$work/mrewrite" ./go/toolchain/mrewrite)

# Acceptance tests of the patched toolchain.
rm -rf "$work/mlit"
cp -R "$here/testdata/mlit" "$work/mlit"
mv "$work/mlit/mlit_test.go.txt" "$work/mlit/mlit_test.go"
(cd "$work/mlit" && gopatched test -count=1 ./... && gopatched vet ./...) > "$results/acceptance.txt" 2>&1
for bad in "$here"/testdata/bad/*.go.txt; do
	name=$(basename "$bad" .go.txt)
	rm -rf "$work/bad" && mkdir "$work/bad" && cp "$bad" "$work/bad/x.go"
	if (cd "$work/bad" && gopatched build x.go) > "$results/bad-$name.txt" 2>&1; then
		echo "error case $name compiled" >&2
		exit 1
	fi
done

# Package outcomes (pass, fail, skip) of go test -json output, one per line.
outcomes() {
	jq -r 'select(.Test == null and (.Action == "pass" or .Action == "fail" or .Action == "skip")) | "\(.Package) \(.Action)"' "$1" | sort -u
}

# runtests <go-function> <dir> <name>
runtests() {
	(cd "$2" && "$1" test -short -count=1 -json ./... > "$results/$3.json" 2> "$results/$3.err") || true
	outcomes "$results/$3.json" > "$results/$3.outcomes"
}

for spec in "$@"; do
	url=${spec%@*}
	rev=$([ "$spec" = "$url" ] && echo HEAD || echo "${spec#*@}")
	name=$(basename "$url" .git)
	dir=$work/repos/$name
	rm -rf "$dir" "$dir.rewritten"
	git clone -q "$url" "$dir"
	git -C "$dir" checkout -q "$rev"
	git -C "$dir" rev-parse HEAD > "$results/$name.commit"

	runtests gostock "$dir" "$name.stock"
	runtests gopatched "$dir" "$name.patched"
	cp -R "$dir" "$dir.rewritten"
	"$work/mrewrite" "$dir.rewritten" > "$results/$name.rewrite"
	runtests gopatched "$dir.rewritten" "$name.rewritten"

	same() { cmp -s "$results/$name.stock.outcomes" "$results/$name.$1.outcomes" && echo same || echo DIFFERENT; }
	printf '%s %s packages=%s pass=%s fail=%s patched=%s rewritten=%s %s\n' "$name" "$(cut -c1-12 "$results/$name.commit")" \
		"$(wc -l < "$results/$name.stock.outcomes" | tr -d ' ')" \
		"$(grep -c ' pass$' "$results/$name.stock.outcomes" || true)" \
		"$(grep -c ' fail$' "$results/$name.stock.outcomes" || true)" \
		"$(same patched)" "$(same rewritten)" "$(cat "$results/$name.rewrite")" | tee -a "$results/summary.txt"
done
