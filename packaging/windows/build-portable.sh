#!/bin/bash
# W4 (ROADMAP.md §7.4): the portable Windows package.
#
# What this guarantees, and why each part is here:
#
#  * the bytes you ship are the bytes you built - the sha256 of the exe inside
#    the archive is compared with the sha256 of the exe in the build directory,
#    and after unzipping it is compared a third time. The release-0.3 lesson
#    (`verify_installed`) was that a green "package built" and a green "the app
#    runs" say nothing if they are not the same file;
#  * the unzipped package runs on a machine with no Qt and no compiler, which is
#    checked by running it with a PATH that contains neither. windeployqt is
#    run with --no-compiler-runtime (it would otherwise be a silent dependency
#    on whoever built the package), so the MinGW runtime is copied by name -
#    three files, and a list is easier to audit than a directory copy;
#  * `traduku --selftest` works from the package. That needs the *offscreen*
#    platform plugin, which windeployqt does not deploy because a normal GUI
#    app never asks for it - and --selftest is the command a user runs to prove
#    an install, including over ssh or from a session with no desktop.
#
# Usage: build-portable.sh [--build DIR] [--out DIR] [--qt PREFIX] [--no-build]
set -euo pipefail

build=build-win
out=dist
qt="${QTDIR:-C:/Qt/6.8.3/mingw_64}"
do_build=1

while [ $# -gt 0 ]; do
    case "$1" in
        --build) build="$2"; shift 2 ;;
        --out)   out="$2"; shift 2 ;;
        --qt)    qt="$2"; shift 2 ;;
        --no-build) do_build=0; shift ;;
        *) echo "usage: $0 [--build DIR] [--out DIR] [--qt PREFIX] [--no-build]" >&2; exit 2 ;;
    esac
done

fail() { echo "FAIL: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1 || fail "$1 not found"; }
here="$(cd "$(dirname "$0")" && pwd)"

# cmake is only needed when this script also builds; --no-build must work in a
# shell that has the archive tools and nothing else, which is how the package
# gets re-verified after the build machine has gone away.
[ "$do_build" = 1 ] && have cmake
have sha256sum
[ -x "$qt/bin/windeployqt.exe" ] || fail "windeployqt not under $qt"

# Where the MinGW runtime DLLs live: next to the compiler CMake actually used.
mingwbin="$(cd "$(dirname "$(sed -n 's/^CMAKE_CXX_COMPILER:.*=//p' \
    "$build/CMakeCache.txt" | tr -d '\r' | tr -d '"')")" 2>/dev/null && pwd -W)" \
    || fail "cannot locate the compiler directory from $build/CMakeCache.txt"

exe="$build/bin/traduku.exe"
[ -f "$exe" ] || fail "$exe missing (run cmake --build first, or drop --no-build)"

if [ "$do_build" = 1 ]; then
    cmake --build "$build"
    ctest --test-dir "$build" --output-on-failure || fail "tests are not green"
fi

stage="$out/portable"
rm -rf "$stage"
mkdir -p "$stage"
cp "$exe" "$stage/traduku.exe" || fail "could not stage the binary"

# Qt's own deployment, the MinGW runtime by name and the headless platform -
# all of it lives in deploy-runtime.sh, because the same directory has to be
# producible by hand for build-win/bin (double-clicking a build that only
# worked in a shell with Qt on PATH is the bug that script was written for).
"$here/deploy-runtime.sh" --build "$build" --qt "$qt" --mingw-bin "$mingwbin" "$stage" \
    || fail "runtime deployment into $stage failed"

version="$("$BASH" "$here/run-isolated.sh" "$stage/traduku.exe" --version | sed -n 's/^traduku \([0-9.]*\).*/\1/p')"
[ -n "$version" ] || fail "could not read the version from the staged binary"
name="traduku-$version-win64-portable"

mkdir -p "$out"
rm -f "$out/$name.zip"
# The archive is written by mkzip.sh, not by a `tar` line here: the layout of a
# shipped zip is a release contract, and a contract with one sentence in it
# (`-C <stage> .`, which stores every member as `./traduku.exe`) has to live in
# one file that has a test of its own. 0.4.0 shipped exactly that archive and
# Windows called it "Compressed (zipped) Folder Invalid".
"$here/mkzip.sh" "$out/$name.zip" "$stage" || fail "could not write the archive"

built_sha="$(sha256sum "$exe" | cut -d' ' -f1)"
zip_sha="$(sha256sum "$out/$name.zip" | cut -d' ' -f1)"
printf '%s  %s\n' "$zip_sha"  "$name.zip"        >  "$out/SHA256SUMS"
printf '%s  %s\n' "$built_sha" "$name/traduku.exe" >> "$out/SHA256SUMS"

# --- the release checks: the archive must open with the reader users have ----
# bsdtar wrote it and bsdtar reading it back proves nothing about Explorer;
# check-portable-zip.sh is the layer that asks zipfldr, and then runs what
# zipfldr produced with no Qt on PATH.
"$here/check-portable-zip.sh" "$out/$name.zip" "$stage" \
    || fail "the archive is not a package a user can open"

# --- and the bytes are the bytes: unpack, compare, run -----------------------
# Unpack with bsdtar (Windows' tar reads and writes zip; the GNU tar on PATH
# does not, and says "this does not look like a tar archive"), and with Python's
# zipfile where bsdtar is absent. The 0.4.0 lesson was that a reader chosen for
# convenience is a reader that lies, so the choice is named, not inherited.
bsdtar="/c/Windows/System32/tar.exe"
[ -x "$bsdtar" ] || bsdtar="$(command -v tar.exe || true)"
[ -n "${bsdtar:-}" ] && [ -x "${bsdtar:-}" ] || bsdtar=""
check="$(mktemp -d)"
trap 'rm -rf "$check"' EXIT
if [ -n "$bsdtar" ]; then
    "$bsdtar" -xf "$out/$name.zip" -C "$check" || fail "the archive does not unpack (bsdtar)"
elif command -v python >/dev/null 2>&1 || command -v python3 >/dev/null 2>&1; then
    py="$(command -v python || command -v python3)"
    "$py" -c 'import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' \
        "$(cd "$out" && pwd)/$name.zip" "$check" || fail "the archive does not unpack (zipfile)"
else
    fail "no reader to unpack the archive with (need bsdtar or python)"
fi
[ -f "$check/traduku.exe" ] || fail "the unpacked archive has no traduku.exe at its root"
unpacked_sha="$(sha256sum "$check/traduku.exe" | cut -d' ' -f1)"
[ "$unpacked_sha" = "$built_sha" ] \
    || fail "the archive holds a different binary than the build produced:
  built   $built_sha
  shipped $unpacked_sha"

case "$(uname -s)" in
MINGW*|MSYS*|CYGWIN*) on_windows=1 ;;
*) on_windows=0 ;;
esac
if [ "$on_windows" = 1 ]; then
    # No Qt, no compiler on PATH: only the shipped DLLs may be loadable. The
    # system dir stays, because Windows itself is not in the package.
    syspath="$(dirname "$(command -v cmd.exe 2>/dev/null || echo /c/Windows/System32/cmd.exe)")"
    if ! (cd "$check" && PATH="$syspath" "$BASH" "$here/run-isolated.sh" \
            ./traduku.exe --version >/dev/null); then
        fail "the unzipped package does not run without Qt on PATH - a shipped \
DLL is missing (this is the check that finds it, not the build)"
    fi
    if ! (cd "$check" && PATH="$syspath" "$BASH" "$here/run-isolated.sh" \
            ./traduku.exe --selftest | grep -q '^SELFTEST PASSED'); then
        fail "the unzipped package cannot pass its own --selftest"
    fi
fi

echo "ok  $out/$name.zip"
echo "    sha256 $zip_sha"
echo "    exe    $built_sha"
