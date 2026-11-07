#!/usr/bin/env bash
# Assemble what the GitHub release uploads, in one directory, with one
# SHA256SUMS that names every file a user can click.
#
# Why a script and not a hand-made list: the release page is the only place
# where the two packaging channels meet, and every previous release was
# assembled by hand. Hand-assembled sets drift - the checksum file then names a
# file nobody uploaded, or stays silent about one that is there, and a user who
# runs `sha256sum -c SHA256SUMS` learns nothing about what they downloaded.
#
# Inputs are the artifacts as built, not rebuilt: Arch packages from
# packaging/arch/mkrelease.sh (release-out/), Debian packages from the grid's
# container build (.cache/vm-deb/). If a file is missing this refuses; an
# incomplete release is published as often as an incomplete build.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
SRC_ARCH=${1:-$ROOT/release-out}
SRC_DEB=${2:-$ROOT/.cache/vm-deb}
OUT=${3:-$ROOT/release-assets}

VER=$(sed -n 's/^project(traduku VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)
[ -n "$VER" ] || { echo "mkrelease-assets.sh: no version in CMakeLists.txt" >&2; exit 1; }

need() { # need <path> <hint>
  [ -f "$1" ] || { echo "mkrelease-assets.sh: missing $(basename "$1") - $2" >&2; exit 1; }
}

rm -rf "$OUT"; mkdir -p "$OUT"

# Arch: both flavours, each with its detached signature.
for pkg in traduku traduku-gnome; do
  need "$SRC_ARCH/$pkg-$VER-1-x86_64.pkg.tar.zst" "build it with packaging/arch/mkrelease.sh"
  need "$SRC_ARCH/$pkg-$VER-1-x86_64.pkg.tar.zst.sig" "makepkg signs with --sign; check TRADUKU_SIGN_KEY"
  cp "$SRC_ARCH/$pkg-$VER-1-x86_64.pkg.tar.zst"{,.sig} "$OUT/"
done

# Debian/Ubuntu: both flavours. Unsigned by design - apt does not verify these,
# and a signature nobody's keyring can check would only add a failure mode.
for pkg in traduku traduku-gnome; do
  need "$SRC_DEB/${pkg}_${VER}-1_amd64.deb" "run: make guest VM=<a-$pkg-VM> (built in the deb-builder container)"
  cp "$SRC_DEB/${pkg}_${VER}-1_amd64.deb" "$OUT/"
done

# Source, so nobody has to trust a binary.
need "$SRC_ARCH/traduku-$VER.tar.gz" "packaging/arch/mkrelease.sh makes it from git archive HEAD"
cp "$SRC_ARCH/traduku-$VER.tar.gz" "$OUT/"

# The public half of the key the Arch packages are signed with. Without it the
# .sig files are decoration nobody can check, and `pacman -U` refuses the
# package outright (SigLevel=Required covers local files).
need "$ROOT/packaging/arch/traduku-release.asc" "the release key's public half, tracked in the repo"
cp "$ROOT/packaging/arch/traduku-release.asc" "$OUT/"

# One file naming everything else, and nothing that is not there.
(cd "$OUT" && rm -f SHA256SUMS && sha256sum -- ./* > SHA256SUMS)

echo "Release assets for $VER in $OUT:"
ls -1 "$OUT"
echo
echo "check: (cd $OUT && sha256sum -c SHA256SUMS)"
