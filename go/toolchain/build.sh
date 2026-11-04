#!/usr/bin/env bash
#
# build.sh — build a Go toolchain that accepts matchertext literals M"(...)",
# M"[...]", and M"{...}" (see matchertext.patch).
#
# Usage: ./build.sh <dir>
#   Clones Go at $GO_VERSION into <dir>/go, applies the patch, and builds it with
#   the installed Go. Use the result with GOROOT=<dir>/go PATH=<dir>/go/bin:$PATH:
#   an inherited GOROOT would otherwise select the stock toolchain.

set -euo pipefail

GO_VERSION=${GO_VERSION:-go1.27.1}
here=$(cd "$(dirname "$0")" && pwd)
dir=${1:?usage: build.sh <dir>}

mkdir -p "$dir"
if [ ! -d "$dir/go" ]; then
	git clone -q --depth 1 --branch "$GO_VERSION" https://go.googlesource.com/go "$dir/go"
	git -C "$dir/go" apply "$here/matchertext.patch"
fi
bootstrap=$(go env GOROOT)
(cd "$dir/go/src" && GOROOT_BOOTSTRAP="$bootstrap" ./make.bash)
