#!/usr/bin/env bash
# Fetches the analyzer's dependencies into extern/ at the commits the evaluation
# was run with: the Serde JSON library and tree-sitter with its grammars.
#
# Usage: ./fetch-extern.sh   (from any directory; already fetched ones are kept)
#
# Requires git, curl, unzip, and shasum. Serde comes as an archive from its
# anonymized mirror, checked against a hash of its files; the rest is cloned from
# GitHub at the pinned commits.
set -euo pipefail
cd "$(dirname "$0")/extern"

SERDE_ZIP="https://anonymous.4open.science/api/repo/Serde-942F/zip"
# SHA-256 over the sorted list of the archive's file hashes.
SERDE_SHA256="423ca2760df34a13d1ab2cea57081d531a8e11d091bc1d2d06b0d2ab844cf328"

# name, repository, pinned commit
GRAMMARS=(
  "tree-sitter https://github.com/tree-sitter/tree-sitter.git b1fa9725cf7bad668ac98b9fd3ec303f1e9076ec"
  "tree-sitter-c-sharp https://github.com/tree-sitter/tree-sitter-c-sharp.git cac6d5fb595f5811a076336682d5d595ac1c9e85"
  "tree-sitter-dart https://github.com/UserNobody14/tree-sitter-dart.git a9bdfa3db2fbc9b9f12c93450d04a671f33a5102"
  "tree-sitter-elixir https://github.com/elixir-lang/tree-sitter-elixir.git 7937d3b4d65fa574163cfa59394515d3c1cf16f4"
  "tree-sitter-erlang https://github.com/WhatsApp/tree-sitter-erlang.git e446ec60022a7cafe157805742b41c04b499cc5d"
  "tree-sitter-glsl https://github.com/tree-sitter-grammars/tree-sitter-glsl.git 24a6c8ef698e4480fecf8340d771fbcb5de8fbb4"
  "tree-sitter-go https://github.com/tree-sitter/tree-sitter-go.git 2346a3ab1bb3857b48b29d779a1ef9799a248cd7"
  "tree-sitter-haskell https://github.com/tree-sitter/tree-sitter-haskell.git 0975ef72fc3c47b530309ca93937d7d143523628"
  "tree-sitter-hlsl https://github.com/tree-sitter-grammars/tree-sitter-hlsl.git bab9111922d53d43668fabb61869bec51bbcb915"
  "tree-sitter-java https://github.com/tree-sitter/tree-sitter-java.git e10607b45ff745f5f876bfa3e94fbcc6b44bdc11"
  "tree-sitter-javascript https://github.com/tree-sitter/tree-sitter-javascript.git 58404d8cf191d69f2674a8fd507bd5776f46cb11"
  "tree-sitter-kotlin https://github.com/fwcd/tree-sitter-kotlin.git f66d2908542e93c0204c6c241f794afe4e9cd5d1"
  "tree-sitter-lua https://github.com/tree-sitter-grammars/tree-sitter-lua.git 10fe0054734eec83049514ea2e718b2a56acd0c9"
  "tree-sitter-objc https://github.com/tree-sitter-grammars/tree-sitter-objc.git 181a81b8f23a2d593e7ab4259981f50122909fda"
  "tree-sitter-ocaml https://github.com/tree-sitter/tree-sitter-ocaml.git 6902a86ab5b3b80c622030210aae2d8cb95eb775"
  "tree-sitter-perl https://github.com/tree-sitter-perl/tree-sitter-perl.git 9b651c0ca29a2c581e2bb07815e17432ac3820d3"
  "tree-sitter-php https://github.com/tree-sitter/tree-sitter-php.git 3f2465c217d0a966d41e584b42d75522f2a3149e"
  "tree-sitter-python https://github.com/tree-sitter/tree-sitter-python.git 26855eabccb19c6abf499fbc5b8dc7cc9ab8bc64"
  "tree-sitter-r https://github.com/r-lib/tree-sitter-r.git 0e6ef7741712c09dc3ee6e81c42e919820cc65ef"
  "tree-sitter-ruby https://github.com/tree-sitter/tree-sitter-ruby.git ad907a69da0c8a4f7a943a7fe012712208da6dee"
  "tree-sitter-rust https://github.com/tree-sitter/tree-sitter-rust.git 77a3747266f4d621d0757825e6b11edcbf991ca5"
  "tree-sitter-scala https://github.com/tree-sitter/tree-sitter-scala.git 4d081d98670ff6e98ca42c085294fc75eec15e1d"
  "tree-sitter-swift https://github.com/alex-pinkus/tree-sitter-swift.git a923ac6d78d6f3132d996ecd189a4c7ca13c8d9d"
  "tree-sitter-typescript https://github.com/tree-sitter/tree-sitter-typescript.git 75b3874edb2dc714fb1fd77a32013d0f8699989f"
)

tree_hash() {
  (cd "$1" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 shasum -a 256 | shasum -a 256 | cut -d' ' -f1)
}

if [ -f Serde/CMakeLists.txt ] && [ "$(tree_hash Serde)" = "$SERDE_SHA256" ]; then
  echo "Serde: present"
else
  echo "Serde: downloading"
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' EXIT
  curl -fsSL -o "$tmp/serde.zip" "$SERDE_ZIP"
  unzip -q "$tmp/serde.zip" -d "$tmp/Serde"
  if [ "$(tree_hash "$tmp/Serde")" != "$SERDE_SHA256" ]; then
    echo "Serde: the downloaded archive does not match the pinned version" >&2
    exit 1
  fi
  rm -rf Serde
  mv "$tmp/Serde" Serde
fi

for entry in "${GRAMMARS[@]}"; do
  read -r name url commit <<< "$entry"
  dir="tree-sitter/$name"
  if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" = "$commit" ]; then
    echo "$name: present"
    continue
  fi
  echo "$name: fetching ${commit:0:12}"
  rm -rf "$dir"
  git init -q "$dir"
  git -C "$dir" fetch -q --depth 1 "$url" "$commit"
  git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
done
echo "extern/ is ready; build with: make train build"
