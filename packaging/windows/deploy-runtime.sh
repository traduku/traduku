#!/usr/bin/env bash
#
# Make a directory holding traduku.exe able to start on its own.
#
# Why this exists as its own script: traduku.exe built by cmake imports
# Qt6Core.dll and friends, and the loader finds those only through PATH. A build
# shell that exports Qt's bin satisfies it, so `--version` works there - and the
# same double-click in Explorer answers "the code execution cannot proceed
# because Qt6Core.dll was not found". Measured: rc 0xc0000135
# (STATUS_DLL_NOT_FOUND) with a PATH of System32 alone. That dialog is raised by
# the loader before a single line of our code runs, so no message of ours can
# make it kinder; the DLLs simply have to sit beside the exe.
#
#   usage: deploy-runtime.sh [--build DIR] [--qt PREFIX] [--mingw-bin DIR] <dir>
#
# <dir> holds traduku.exe. Used by build-portable.sh for the release directory,
# and meant to be run by hand once after a build so that
# `build-win/bin/traduku.exe` can be double-clicked as well:
#
#   packaging/windows/deploy-runtime.sh build-win/bin
#
# Deliberately not wired into the build: 78 MB of copies on every link would
# make the inner loop hurt, and the tests reach Qt through PATH anyway.

set -euo pipefail

build=build-win
qt="${QTDIR:-}"
mingwbin="${MINGW_BIN:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --build)     build="$2"; shift 2 ;;
        --qt)        qt="$2"; shift 2 ;;
        --mingw-bin) mingwbin="$2"; shift 2 ;;
        -h|--help)   sed -n '2,20p' "$0"; exit 0 ;;
        -*)          echo "unknown option: $1" >&2; exit 2 ;;
        *)           target="$1"; shift ;;
    esac
done

[ -n "${target:-}" ] || { echo "usage: $0 [options] <dir containing traduku.exe>" >&2; exit 2; }
[ -f "$target/traduku.exe" ] || { echo "no traduku.exe in $target" >&2; exit 2; }

# Both prefixes are read from the build's own CMakeCache when not given: the
# Qt that the exe was linked against is the only one whose DLLs it may load, and
# guessing a version that happens to be installed is how a package gets built
# against one Qt and shipped with another.
cache="$build/CMakeCache.txt"
if [ -z "$qt" ] || [ -z "$mingwbin" ]; then
    [ -f "$cache" ] || { echo "no $cache; pass --qt and --mingw-bin" >&2; exit 2; }
fi
if [ -z "$qt" ]; then
    qt="$(sed -n 's/^CMAKE_PREFIX_PATH:.*=//p' "$cache" | tr -d '\r' | head -1)"
fi
if [ -z "$mingwbin" ]; then
    cxx="$(sed -n 's/^CMAKE_CXX_COMPILER:.*=//p' "$cache" | tr -d '\r' | tr -d '"')"
    [ -n "$cxx" ] || { echo "cannot read the compiler from $cache" >&2; exit 2; }
    mingwbin="$(cd "$(dirname "$cxx")" 2>/dev/null && pwd -W)" \
        || { echo "compiler directory for $cxx is gone" >&2; exit 2; }
fi
[ -x "$qt/bin/windeployqt.exe" ] || { echo "windeployqt not under $qt" >&2; exit 1; }

# --no-compiler-runtime: windeployqt picks the MinGW runtime by pattern and
# version, so the three DLLs we actually need are copied by name below, where a
# reader can see the list. --no-translations: the app ships no .qm files, and
# Qt's own catalogues are a directory of strings this program never reads.
"$qt/bin/windeployqt.exe" --release --no-translations --no-compiler-runtime \
    --no-system-d3d-compiler --no-opengl-sw \
    "$target/traduku.exe" >/dev/null || { echo "windeployqt refused" >&2; exit 1; }

for dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
    [ -f "$target/$dll" ] && continue
    [ -f "$mingwbin/$dll" ] || { echo "$dll not found next to the compiler at $mingwbin" >&2; exit 1; }
    cp "$mingwbin/$dll" "$target/$dll"
done

# The headless platform, shipped on purpose: `traduku --selftest` is what a user
# runs to prove an install, and it is also the only thing a machine with no
# desktop session can run - the app itself asks for offscreen when there is no
# screen. windeployqt does not deploy it, because a normal GUI app never asks,
# so without this line the diagnostic meant to explain a broken install becomes
# one: Qt's fatal "could not find the Qt platform plugin offscreen".
mkdir -p "$target/platforms"
[ -f "$target/platforms/qoffscreen.dll" ] \
    || cp "$qt/plugins/platforms/qoffscreen.dll" "$target/platforms/qoffscreen.dll" \
    || { echo "qoffscreen.dll missing from $qt" >&2; exit 1; }

echo "ok    $target is self-contained"
