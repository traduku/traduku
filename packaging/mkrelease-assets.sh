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

# Windows: the portable zip, built on a Windows machine by
# packaging/windows/build-portable.sh. It is in the set because it is the only
# artifact a Windows user can click - and it is checked before it is copied,
# because the 0.4.0 zip passed every build check and could not be extracted on
# Windows at all (see packaging/windows/check-portable-zip.sh).
SRC_WIN=${4:-$ROOT/dist}
win_zip="$(find "$SRC_WIN" -maxdepth 1 -name "traduku-$VER-win64-portable.zip" 2>/dev/null | head -1)"
if [ -n "$win_zip" ]; then
    "$ROOT/packaging/windows/check-portable-zip.sh" "$win_zip" \
        || { echo "mkrelease-assets.sh: refusing to assemble a release around a Windows package nobody can open" >&2; exit 1; }
    cp "$win_zip" "$OUT/"
else
    # Not a silence: a release assembled on a Linux machine without the Windows
    # artifact is a decision, and the line below says which one was made.
    echo "mkrelease-assets.sh: NOTE no traduku-$VER-win64-portable.zip in $SRC_WIN - the release set will have no Windows package"
fi

# The Windows installer, when this machine has one. An .msi is an OLE compound
# file, so the magic number is the cheapest statement that the bytes are a
# database and not a truncated download. The real reading - tables, embedded
# cabinet, one upgrade code, per-machine - is packaging/windows/msi-inspect.ps1,
# and it runs where msi.dll exists: on Windows, in build-msi.sh and in the `msi`
# gate. Here the file is copied and the limit of what this machine can say about
# it is written down, because "checked" is a word a release script must not use
# for a thing it did not check.
win_msi="$(find "$SRC_WIN" -maxdepth 1 -name "traduku-$VER-x64.msi" 2>/dev/null | head -1)"
if [ -n "$win_msi" ]; then
    magic="$(od -An -tx1 -N8 "$win_msi" | tr -dc '0-9a-f')"
    if [ "$magic" != "d0cf11e0a1b11ae1" ]; then
        echo "mkrelease-assets.sh: $win_msi is not an OLE compound file (magic: $magic) - an .msi starts d0cf11e0a1b11ae1" >&2
        exit 1
    fi
    cp "$win_msi" "$OUT/"
    echo "mkrelease-assets.sh: traduku-$VER-x64.msi copied; its tables are inspected on Windows (msi-inspect.ps1), not here - msi.dll does not exist on Linux"
else
    echo "mkrelease-assets.sh: NOTE no traduku-$VER-x64.msi in $SRC_WIN - the release set will have no Windows installer"
fi

# One file naming everything else, and nothing that is not there.
(cd "$OUT" && rm -f SHA256SUMS && sha256sum -- ./* > SHA256SUMS)

echo "Release assets for $VER in $OUT:"
ls -1 "$OUT"
echo
echo "check: (cd $OUT && sha256sum -c SHA256SUMS)"
