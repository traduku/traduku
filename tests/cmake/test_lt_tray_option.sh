#!/bin/sh
# Stage 6b: the LT_TRAY switch is a build-time guarantee, so it is tested at
# build-configuration time. A typo here (-DLT_TRAY=qtys) would otherwise
# configure happily and ship something other than what the packager asked for
# - the exact mistake this switch exists to avoid.
#
# Usage: test_lt_tray_option.sh <source dir> <scratch dir> [shell] [cmake args...]
#
# The shell name and the extra configure arguments come from the caller rather
# than from here: which LT_SHELL values exist is a platform fact (Windows has
# one, Linux has gnome/kde), and how to find a compiler at all is a machine fact
# (CMAKE_PREFIX_PATH for Qt, PATH for MinGW). Hardcoding either made this gate
# Linux-only in a way that looked like a passing suite rather than a missing one.
set -u
src="$1"
tmp="$2"
shell="${3:-gnome}"
if [ "$#" -ge 3 ]; then shift 3; else shift 2; fi
cmake_extra=("$@")
# A scratch directory left over from an earlier run carries its own
# CMakeCache.txt - and a cached generator makes the next configure fail with a
# message about generators, while what the gate is checking is LT_TRAY.
rm -rf "$tmp"
mkdir -p "$tmp"
fail() { echo "FAIL: $*" >&2; exit 1; }
have_cmake=0
command -v cmake >/dev/null 2>&1 || fail "cmake not found"

# 1. an invalid value must be a configure error that names the option.
#    "kde" belongs in this list on purpose: the KStatusNotifierItem tray is
#    deleted, and an old `-DLT_TRAY=kde` in a packaging recipe has to fail
#    here rather than quietly build something else.
for bad in qyt kde ""; do
    out=$(cmake -S "$src" -B "$tmp/bad-$bad" -DLT_SHELL="$shell"           "${cmake_extra[@]}" -DLT_TRAY="$bad" 2>&1)
    rc=$?
    if [ "$rc" -eq 0 ]; then
        fail "configure accepted LT_TRAY=$bad (must be fatal)"
    fi
    echo "$out" | grep -qi 'LT_TRAY' \
        || fail "the fatal error does not mention LT_TRAY: $out"
done

# 2. the surviving values configure (the tray switch is orthogonal to the
#    shell, so a package must be able to pass either value without a surprise).
for v in qt none; do
    cmake -S "$src" -B "$tmp/$v" -DLT_SHELL="$shell" "${cmake_extra[@]}"         -DLT_TRAY="$v" >"$tmp/$v.log" 2>&1         || fail "LT_TRAY=$v rejected: $(tail -5 "$tmp/$v.log")"
done

# 3. the value reaches the compiler, otherwise the shell could not honour it.
grep -rq 'LT_TRAY_DEFAULT' "$tmp/qt/compile_commands.json" 2>/dev/null \
    || fail "LT_TRAY does not reach the traduku target as LT_TRAY_DEFAULT"

# 4. every shell must know its version and tray at compile time. The KDE shell
#    once printed `traduku --version` as "traduku" with no number, which is how
#    an operator ends up unable to say which build is in the panel.
if ! cmake -S "$src" -B "$tmp/kdever" -DLT_SHELL=kde "${cmake_extra[@]}" >"$tmp/kdever.log" 2>&1; then
    echo "note: KDE shell not configurable here (KF6 missing), skipping version check"
else
    grep -q 'LT_VERSION' "$tmp/kdever/compile_commands.json" 2>/dev/null \
        || fail "the KDE shell has no LT_VERSION - 'traduku --version' prints nothing"
    grep -q 'LT_TRAY_DEFAULT' "$tmp/kdever/compile_commands.json" 2>/dev/null \
        || fail "the KDE shell has no LT_TRAY_DEFAULT"
fi

echo "ok"
