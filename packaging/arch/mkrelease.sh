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
    --prefix="traduku-${VER}/" HEAD \
    -o "$OUT/traduku-${VER}.tar.gz"
cp "$HERE/PKGBUILD" "$OUT/"
# makepkg resolves install= (and any .install hook) relative to the directory
# it reads the PKGBUILD from, not from $HERE: copying only the PKGBUILD gave
# "install file (traduku-gnome.install) does not exist" and - because the
# caller did not check for a package - the grid then tested whatever was
# already installed. Any auxiliary file the PKGBUILD names comes along.
for aux in "$HERE"/*.install "$HERE"/*.hook; do
  [ -f "$aux" ] && cp "$aux" "$OUT/"
done
# --sign: detached .pkg.tar.zst.sig next to the package — pacman -U verifies
# it automatically when installing (the release channel ships both files).
(cd "$OUT" && makepkg -sf --noconfirm --needed --sign "$@")
echo
echo "Release artifacts in $OUT:"
ls -1 "$OUT"
