#!/usr/bin/env bash
# Builds the Debian/Ubuntu package edith_<version>-1_<arch>.deb from a built ./edith.
#
# Usage:  make STATIC=1 && packaging/debian/build-deb.sh [output-dir]
#
# The binary should be linked statically (make STATIC=1): then the package has no
# dependencies and installs on every Debian and Ubuntu release. Uses dpkg-deb when it is
# installed, otherwise builds the archive with ar and tar (e.g. on Arch Linux).
#
# Environment: MAINTAINER, HOMEPAGE (shown in the package information).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath "${1:-$ROOT/build}")"
VERSION="$(make -s -C "$ROOT" version)"
PKG=edith
REV=1
MAINTAINER="${MAINTAINER:-Dennis Lehmann <mail@dennis-lehmann.de>}"
HOMEPAGE="${HOMEPAGE:-}"

[ -x "$ROOT/edith" ] || { echo "build-deb: build ./edith first (make STATIC=1)" >&2; exit 1; }
case "$(uname -m)" in
  x86_64) ARCH=amd64 ;;
  aarch64) ARCH=arm64 ;;
  *) ARCH="$(uname -m)" ;;
esac
DEPENDS=""
if file -L "$ROOT/edith" | grep -q "dynamically linked"; then
  echo "build-deb: ./edith is linked dynamically; using the dependencies libc6 and libstdc++6" >&2
  DEPENDS="libc6, libstdc++6, libgcc-s1"
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
D="$WORK/root"
install -Dm755 "$ROOT/edith" "$D/usr/bin/edith"
install -Dm644 "$ROOT/docs/edith.1" "$D/usr/share/man/man1/edith.1"
gzip -9n "$D/usr/share/man/man1/edith.1"
install -Dm644 "$ROOT/packaging/share/edith.desktop" "$D/usr/share/applications/edith.desktop"
install -Dm644 "$ROOT/packaging/share/edith.svg" "$D/usr/share/icons/hicolor/scalable/apps/edith.svg"
install -Dm644 "$ROOT/README.md" "$D/usr/share/doc/$PKG/README.md"
gzip -9nc "$ROOT/CHANGELOG.md" > "$D/usr/share/doc/$PKG/changelog.gz"
"$ROOT/tools/third-party-licenses.sh" > "$D/usr/share/doc/$PKG/THIRD_PARTY_LICENSES"
{
  echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
  echo "Upstream-Name: Edith"
  [ -n "$HOMEPAGE" ] && echo "Source: $HOMEPAGE"
  echo
  echo "Files: *"
  echo "Copyright: 2026 Dennis Lehmann"
  echo "License: MIT"
  echo
  echo "Files: third_party/tree-sitter/*"
  echo "License: MIT"
  echo
  echo "Files: third_party/grammars/*"
  echo "License: MIT or Apache-2.0 or CC0-1.0 or Unlicense"
  echo
  echo "Files: third_party/queries/*"
  echo "Copyright: nvim-treesitter contributors"
  echo "License: Apache-2.0"
  echo
  echo "Comment: The license texts of all third-party code are in"
  echo " /usr/share/doc/$PKG/THIRD_PARTY_LICENSES."
  echo
  echo "License: MIT"
  sed 's/^$/./; s/^/ /' "$ROOT/LICENSE"
} > "$D/usr/share/doc/$PKG/copyright"
chmod 644 "$D/usr/share/doc/$PKG/"*

mkdir -p "$D/DEBIAN"
SIZE="$(du -sk --exclude=DEBIAN "$D" | cut -f1)"
{
  echo "Package: $PKG"
  echo "Version: $VERSION-$REV"
  echo "Architecture: $ARCH"
  echo "Maintainer: $MAINTAINER"
  echo "Installed-Size: $SIZE"
  [ -n "$DEPENDS" ] && echo "Depends: $DEPENDS"
  echo "Section: editors"
  echo "Priority: optional"
  [ -n "$HOMEPAGE" ] && echo "Homepage: $HOMEPAGE"
  echo "Description: friendly text editor for the terminal"
  echo " Edith is a text editor for the terminal in the spirit of the classic MS-DOS"
  echo " EDIT: pull-down menus, dialogs and mouse support, plus split windows and"
  echo " tabs, syntax highlighting with tree-sitter for more than 50 languages, code"
  echo " formatting and an optional, faithful Vim mode."
} > "$D/DEBIAN/control"
(cd "$D" && find usr -type f -print0 | sort -z | xargs -0 md5sum) > "$D/DEBIAN/md5sums"
chmod 644 "$D/DEBIAN/control" "$D/DEBIAN/md5sums"

DEB="$OUT/${PKG}_${VERSION}-${REV}_${ARCH}.deb"
mkdir -p "$OUT"
if command -v dpkg-deb >/dev/null; then
  dpkg-deb --root-owner-group -Zxz --build "$D" "$DEB" >/dev/null
else
  T="$WORK/ar"
  mkdir -p "$T"
  echo "2.0" > "$T/debian-binary"
  tar --owner=0 --group=0 --numeric-owner -C "$D/DEBIAN" -cJf "$T/control.tar.xz" .
  tar --owner=0 --group=0 --numeric-owner -C "$D" --exclude=./DEBIAN -cJf "$T/data.tar.xz" .
  rm -f "$DEB"
  (cd "$T" && ar -rc "$DEB" debian-binary control.tar.xz data.tar.xz)
fi
echo "$DEB"
