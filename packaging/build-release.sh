#!/usr/bin/env bash
# Builds all files of a release into one directory:
#
#   edith-<version>.tar.gz                    source tarball including third_party/ (make dist)
#   edith-<version>-linux-<arch>.tar.gz       statically linked binary, manual page, licenses
#   edith_<version>-1_<debarch>.deb       Debian / Ubuntu package (also as edith_<debarch>.deb)
#   Edith-<version>-<arch>.AppImage           AppImage (only when appimagetool is available)
#   PKGBUILD                                 Arch Linux, with download URL and checksum
#   SHA256SUMS
#
# Usage:  packaging/build-release.sh [output-dir]      (default: build/release; must be empty)
#
# Expects a clean git tree, the grammars in third_party/ and a statically linked ./edith
# (make STATIC=1). Build the release binaries on an older distribution (the release workflow
# uses Debian bookworm): a static glibc binary only runs on kernels at least as new as the
# one the glibc of the build system was configured for.
#
# Environment: REPO_URL (e.g. https://github.com/user/edith) for the PKGBUILD and the .deb.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(realpath -m "${1:-$ROOT/build/release}")"
VERSION="$(make -s -C "$ROOT" version)"
ARCH="$(uname -m)"
REPO_URL="${REPO_URL:-https://github.com/dlehmann/edith}"

if ! file -L "$ROOT/edith" | grep -qE "statically linked|static-pie linked"; then
  echo "build-release: ./edith must be linked statically (make clean && make STATIC=1)" >&2
  exit 1
fi
mkdir -p "$OUT"
if [ -n "$(ls -A "$OUT")" ]; then
  echo "build-release: $OUT is not empty" >&2
  exit 1
fi

# source tarball
make -s -C "$ROOT" dist >/dev/null
mv "$ROOT/build/edith-$VERSION.tar.gz" "$OUT/"

# binary tarball
BIN="edith-$VERSION-linux-$ARCH"
mkdir -p "$OUT/$BIN"
cp "$ROOT/edith" "$ROOT/docs/edith.1" "$ROOT/LICENSE" "$ROOT/README.md" "$OUT/$BIN/"
"$ROOT/tools/third-party-licenses.sh" > "$OUT/$BIN/THIRD_PARTY_LICENSES"
tar -C "$OUT" -czf "$OUT/$BIN.tar.gz" "$BIN"
rm -rf "${OUT:?}/$BIN"

# Debian package, and the same file without the version for .../releases/latest/download/ links
DEB="$(HOMEPAGE="$REPO_URL" "$ROOT/packaging/debian/build-deb.sh" "$OUT")"
cp "$DEB" "$OUT/edith_${DEB##*_}"

# AppImage
if command -v "${APPIMAGETOOL:-appimagetool}" >/dev/null; then
  "$ROOT/packaging/appimage/build-appimage.sh" "$OUT" >/dev/null
else
  echo "build-release: appimagetool not found, no AppImage" >&2
fi

# PKGBUILD for the source tarball of this release
SUM="$(sha256sum "$OUT/edith-$VERSION.tar.gz" | cut -d' ' -f1)"
sed -e "s|^pkgver=.*|pkgver=$VERSION|" \
    -e "s|^url=.*|url=\"$REPO_URL\"|" \
    -e "s|^source=.*|source=(\"edith-\$pkgver.tar.gz::\$url/releases/download/v\$pkgver/edith-\$pkgver.tar.gz\")|" \
    -e "s|^sha256sums=.*|sha256sums=('$SUM')|" \
    "$ROOT/packaging/arch/PKGBUILD" > "$OUT/PKGBUILD"

(cd "$OUT" && sha256sum -- * > SHA256SUMS)
ls -1 "$OUT"
