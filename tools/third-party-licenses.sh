#!/usr/bin/env bash
# Writes the license texts of all third-party code that is compiled into Edith, for the
# THIRD_PARTY_LICENSES file of the binary packages (the licenses require that their texts
# come with every copy of the binary). Components with the same license text share one entry.
# Usage: tools/third-party-licenses.sh [third_party] > THIRD_PARTY_LICENSES
set -euo pipefail
TP="${1:-$(cd "$(dirname "$0")/.." && pwd)/third_party}"

[ -f "$TP/tree-sitter/LICENSE" ] || { echo "third-party-licenses: no $TP/tree-sitter/LICENSE" >&2; exit 1; }

# component name and license file, in the order of the output
names=("tree-sitter" "nvim-treesitter queries (highlights, injections, indents)")
files=("$TP/tree-sitter/LICENSE" "$TP/queries/LICENSE")
for d in "$TP"/grammars/*/; do
  g="$(basename "$d")"
  [ -f "$d/LICENSE" ] || { echo "third-party-licenses: grammar $g has no LICENSE" >&2; exit 1; }
  names+=("tree-sitter grammar: $g")
  files+=("$d/LICENSE")
done

cat <<'EOF'
Third-party licenses
====================

Edith is released under the MIT License (see LICENSE). The binary contains the following
third-party code, compiled in from third_party/ of the source tarball (downloaded by
tools/fetch-grammars.sh). Their license texts follow below.

The statically linked release binaries also contain the GNU C Library (glibc, GNU Lesser
General Public License 2.1 or later, https://www.gnu.org/software/libc/) and the GNU C++
library and runtime of GCC (libstdc++, libgcc: GNU General Public License 3 with the GCC
Runtime Library Exception, https://gcc.gnu.org/). The source code of Edith is available, so
Edith can be relinked with a modified glibc (make STATIC=1).
EOF

merged=" "  # indexes already printed with an earlier entry (no array: bash 3.2 on macOS)
for i in "${!files[@]}"; do
  [[ "$merged" == *" $i "* ]] && continue
  group=("${names[$i]}")
  for j in "${!files[@]}"; do
    [ "$j" -gt "$i" ] || continue
    if cmp -s "${files[$i]}" "${files[$j]}"; then
      group+=("${names[$j]}")
      merged+="$j "
    fi
  done
  echo
  echo
  echo "------------------------------------------------------------------------------"
  printf '%s\n' "${group[@]}"
  echo "------------------------------------------------------------------------------"
  echo
  cat "${files[$i]}"
done
