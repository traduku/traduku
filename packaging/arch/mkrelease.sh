#!/usr/bin/env bash
# Produce the release tarball and the Arch package (CachyOS/Arch).
# Output: $1 (default: ./release-out), git-tag the HEAD commit yourself after.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
VER="$(sed -n 's/^pkgver=\(.*\)$/\1/p' "$HERE/PKGBUILD")"
OUT="${1:-$ROOT/release-out}"
rm -rf "$OUT"; mkdir -p "$OUT"
git -C "$ROOT" archive --format=tar.gz \
    --prefix="kde-llm-translate-${VER}/" HEAD \
    -o "$OUT/kde-llm-translate-${VER}.tar.gz"
cp "$HERE/PKGBUILD" "$OUT/"
(cd "$OUT" && makepkg -sf --noconfirm --needed)
echo
echo "Release artifacts in $OUT:"
ls -1 "$OUT"
