#!/usr/bin/env bash
# Builds Edith-<version>-<arch>.AppImage from a statically linked ./edith.
#
# Usage:  make STATIC=1 && packaging/appimage/build-appimage.sh [output-dir]
#
# Needs appimagetool (https://github.com/AppImage/appimagetool) in PATH or in $APPIMAGETOOL.
# Since the binary is linked statically, the AppImage contains everything it needs.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath "${1:-$ROOT/build}")"
VERSION="$(make -s -C "$ROOT" version)"
ARCH="$(uname -m)"
TOOL="${APPIMAGETOOL:-appimagetool}"

[ -x "$ROOT/edith" ] || { echo "build-appimage: build ./edith first (make STATIC=1)" >&2; exit 1; }
if ! file -L "$ROOT/edith" | grep -qE "statically linked|static-pie linked"; then
  echo "build-appimage: ./edith must be linked statically (make clean && make STATIC=1)" >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
A="$WORK/Edith.AppDir"
install -Dm755 "$ROOT/edith" "$A/usr/bin/edith"
install -Dm644 "$ROOT/docs/edith.1" "$A/usr/share/man/man1/edith.1"
install -Dm644 "$ROOT/packaging/share/edith.desktop" "$A/usr/share/applications/edith.desktop"
install -Dm644 "$ROOT/packaging/share/edith.svg" "$A/usr/share/icons/hicolor/scalable/apps/edith.svg"
install -Dm644 "$ROOT/LICENSE" "$A/usr/share/doc/edith/LICENSE"
"$ROOT/tools/third-party-licenses.sh" > "$A/usr/share/doc/edith/THIRD_PARTY_LICENSES"
cp "$ROOT/packaging/share/edith.desktop" "$ROOT/packaging/share/edith.svg" "$A/"
ln -s usr/bin/edith "$A/AppRun"

mkdir -p "$OUT"
IMG="$OUT/Edith-$VERSION-$ARCH.AppImage"
ARCH="$ARCH" "$TOOL" --no-appstream "$A" "$IMG" >&2
echo "$IMG"
