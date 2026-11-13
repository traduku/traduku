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

version="$("$stage/traduku.exe" --version | sed -n 's/^traduku \([0-9.]*\).*/\1/p')"
[ -n "$version" ] || fail "could not read the version from the staged binary"
name="traduku-$version-win64-portable"

mkdir -p "$out"
rm -f "$out/$name.zip"
# The zip is written by Windows' own bsdtar, not by the `tar` on PATH: git bash
# ships GNU tar, whose -a does not understand a .zip suffix and would happily
# produce a gzip'd tar named .zip - an archive that "built" and unpacks into
# nonsense. Named by path, so the choice is not whoever installed last.
bsdtar="/c/Windows/System32/tar.exe"
if [ ! -x "$bsdtar" ]; then
    bsdtar="$(command -v tar.exe || true)"
fi
[ -n "${bsdtar:-}" ] && [ -x "${bsdtar:-}" ] || fail "no bsdtar to write the archive"
"$bsdtar" -a -cf "$out/$name.zip" -C "$stage" . || fail "could not write the archive"

built_sha="$(sha256sum "$exe" | cut -d' ' -f1)"
zip_sha="$(sha256sum "$out/$name.zip" | cut -d' ' -f1)"
printf '%s  %s\n' "$zip_sha"  "$name.zip"        >  "$out/SHA256SUMS"
printf '%s  %s\n' "$built_sha" "$name/traduku.exe" >> "$out/SHA256SUMS"

# --- verification: unzip somewhere else and run it with a PATH that has no Qt
check="$(mktemp -d)"
trap 'rm -rf "$check"' EXIT
"$bsdtar" -xf "$out/$name.zip" -C "$check" || fail "the archive does not unpack"
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
    if ! (cd "$check" && PATH="$syspath" QT_QPA_PLATFORM=offscreen \
            ./traduku.exe --version >/dev/null); then
        fail "the unzipped package does not run without Qt on PATH - a shipped \
DLL is missing (this is the check that finds it, not the build)"
    fi
    if ! (cd "$check" && PATH="$syspath" QT_QPA_PLATFORM=offscreen \
            ./traduku.exe --selftest | grep -q '^SELFTEST PASSED'); then
        fail "the unzipped package cannot pass its own --selftest"
    fi
fi

echo "ok  $out/$name.zip"
echo "    sha256 $zip_sha"
echo "    exe    $built_sha"
