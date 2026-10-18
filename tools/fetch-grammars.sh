#!/usr/bin/env bash
# Downloads tree-sitter and the grammars at pinned versions into third_party/.
#
# Versions and queries (highlights.scm, injections.scm, indents.scm) are taken from
# nvim-treesitter, so that each grammar and its queries are guaranteed to match.
#
# Usage:  tools/fetch-grammars.sh            (download everything)
#         tools/fetch-grammars.sh rust go    (re-download single languages)
#         tools/fetch-grammars.sh --queries  (only the query files)
set -euo pipefail

TS_VERSION="v0.27.1"
NVIM_TS_COMMIT="1a4f47b03dfe92d28de7d5654c67327ef008c9e6"

# Languages with their own parser (names as in nvim-treesitter)
LANGS=(
  c cpp c_sharp java kotlin swift scala dart go rust zig
  javascript typescript tsx python ruby perl php php_only lua
  bash fish powershell haskell commonlisp clojure scheme erlang elixir r julia
  sql css scss json html xml dtd markdown markdown_inline ini toml yaml diff
  make cmake dockerfile asm nasm latex hcl terraform nix pascal fortran regex jsdoc
)
# Languages that only provide queries (inherited by others)
QUERY_ONLY=(ecma jsx html_tags)

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

QUERIES_ONLY=0
if [ "${1:-}" = "--queries" ]; then QUERIES_ONLY=1; shift; fi
if [ $# -gt 0 ]; then LANGS=("$@"); fi

fetch_rev() {  # url revision target-dir
  local url="$1" rev="$2" dir="$3"
  mkdir -p "$dir"
  git -C "$dir" init -q
  git -C "$dir" remote add origin "$url"
  git -C "$dir" fetch -q --depth 1 origin "$rev"
  git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
}

TS_CLI=""
ts_cli() {  # downloads the tree-sitter CLI on demand (needed to generate some parsers)
  if [ -z "$TS_CLI" ]; then
    local arch; arch="$(uname -m)"; [ "$arch" = "x86_64" ] && arch=x64; [ "$arch" = "aarch64" ] && arch=arm64
    curl -sSfL "https://github.com/tree-sitter/tree-sitter/releases/download/$TS_VERSION/tree-sitter-linux-$arch.gz" \
      | gunzip > "$WORK/tree-sitter"
    chmod +x "$WORK/tree-sitter"
    TS_CLI="$WORK/tree-sitter"
  fi
  "$TS_CLI" "$@"
}

echo "== tree-sitter $TS_VERSION"
if [ "$QUERIES_ONLY" = 0 ] && { [ ! -f "$TP/tree-sitter/lib/src/lib.c" ] || [ $# -eq 0 ]; }; then
  rm -rf "$TP/tree-sitter"
  git -c advice.detachedHead=false clone -q --depth 1 -b "$TS_VERSION" https://github.com/tree-sitter/tree-sitter.git "$WORK/ts"
  mkdir -p "$TP/tree-sitter"
  cp -r "$WORK/ts/lib/src" "$WORK/ts/lib/include" "$TP/tree-sitter/"
  mkdir -p "$TP/tree-sitter/lib" && mv "$TP/tree-sitter/src" "$TP/tree-sitter/include" "$TP/tree-sitter/lib/"
  cp "$WORK/ts/LICENSE" "$TP/tree-sitter/"
fi

echo "== nvim-treesitter $NVIM_TS_COMMIT"
fetch_rev https://github.com/nvim-treesitter/nvim-treesitter.git "$NVIM_TS_COMMIT" "$WORK/nvim"
PARSERS="$WORK/nvim/lua/nvim-treesitter/parsers.lua"

field() {  # language field
  awk -v L="$1" -v F="$2" '
    $0 ~ "^  " L " = \\{" { p = 1 }
    p && $0 ~ "^    install_info" { ii = 1 }
    p && ii && $0 ~ "^      " F " = " { sub(/^[^=]*= /, ""); gsub(/[\x27,]/, ""); print; exit }
    p && $0 ~ "^  },"  { exit }' "$PARSERS"
}

mkdir -p "$TP/grammars" "$TP/queries"
cp "$WORK/nvim/LICENSE" "$TP/queries/LICENSE"

for lang in "${LANGS[@]}"; do
  [ "$QUERIES_ONLY" = 1 ] && break
  url="$(field "$lang" url)"; rev="$(field "$lang" revision)"
  loc="$(field "$lang" location)"; gen="$(field "$lang" generate)"
  if [ -z "$url" ]; then echo "!! $lang: not found in parsers.lua"; continue; fi
  echo "== $lang  ($url @ ${rev:0:12}${loc:+ in $loc})"
  repo="$WORK/repo-$lang"
  fetch_rev "$url" "$rev" "$repo"
  src="$repo/${loc:-.}"
  if [ "$gen" = "true" ]; then
    echo "   generating parser ..."
    ts_cli_path="$(ts_cli --version >/dev/null && echo "$TS_CLI")"
    (cd "$src" && "$ts_cli_path" generate >/dev/null)
  fi
  dst="$TP/grammars/$lang"
  rm -rf "$dst"; mkdir -p "$dst"
  cp -r "$src/src" "$dst/src"
  # shared files (e.g. common/scanner.h for typescript and php)
  if [ -n "$loc" ] && [ -d "$repo/common" ]; then cp -r "$repo/common" "$dst/common"; fi
  # keep paths like "../../common/scanner.h" valid relative to src/
  if [ -n "$loc" ] && [ -d "$dst/common" ]; then mkdir -p "$dst/x" && mv "$dst/src" "$dst/x/src"; fi
  for lic in LICENSE LICENSE.md LICENSE.txt COPYING COPYING.txt UNLICENSE; do
    [ -f "$repo/$lic" ] && cp "$repo/$lic" "$dst/LICENSE" && break
  done
  rm -f "$dst"/src/*.json "$dst"/src/*.wasm 2>/dev/null || true
done

for q in "${LANGS[@]}" "${QUERY_ONLY[@]}"; do
  qd="$WORK/nvim/runtime/queries/$q"
  [ -d "$qd" ] || continue
  rm -rf "$TP/queries/$q"; mkdir -p "$TP/queries/$q"
  for f in highlights injections indents; do
    [ -f "$qd/$f.scm" ] && cp "$qd/$f.scm" "$TP/queries/$q/"
  done
done
echo "done."
