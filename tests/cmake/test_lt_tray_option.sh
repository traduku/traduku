#!/bin/sh
# Stage 6b: the LT_TRAY switch is a build-time guarantee, so it is tested at
# build-configuration time. A typo here (-DLT_TRAY=qtys) would otherwise
# configure happily and ship something other than what the packager asked for
# - the exact mistake this switch exists to avoid.
#
# Usage: test_lt_tray_option.sh <source dir> <scratch dir>
set -u
src="$1"
tmp="$2"
fail() { echo "FAIL: $*" >&2; exit 1; }
have_cmake=0
command -v cmake >/dev/null 2>&1 || fail "cmake not found"

# 1. an invalid value must be a configure error that names the option.
#    "kde" belongs in this list on purpose: the KStatusNotifierItem tray is
#    deleted, and an old `-DLT_TRAY=kde` in a packaging recipe has to fail
#    here rather than quietly build something else.
for bad in qyt kde ""; do
    out=$(cmake -S "$src" -B "$tmp/bad-$bad" -DLT_SHELL=gnome -DLT_TRAY="$bad" 2>&1)
    rc=$?
    if [ "$rc" -eq 0 ]; then
        fail "configure accepted LT_TRAY=$bad (must be fatal)"
    fi
    echo "$out" | grep -qi 'LT_TRAY' \
        || fail "the fatal error does not mention LT_TRAY: $out"
done

# 2. the surviving values configure (the GNOME shell ignores the value, but a
#    package must be able to pass either without a surprise).
for v in qt none; do
    cmake -S "$src" -B "$tmp/$v" -DLT_SHELL=gnome -DLT_TRAY="$v" \
        >"$tmp/$v.log" 2>&1 \
        || fail "LT_TRAY=$v rejected: $(tail -5 "$tmp/$v.log")"
done

# 3. the value reaches the compiler, otherwise the shell could not honour it.
grep -rq 'LT_TRAY_DEFAULT' "$tmp/qt/compile_commands.json" 2>/dev/null \
    || fail "LT_TRAY does not reach the traduku target as LT_TRAY_DEFAULT"

# 4. every shell must know its version and tray at compile time. The KDE shell
#    once printed `traduku --version` as "traduku" with no number, which is how
#    an operator ends up unable to say which build is in the panel.
if ! cmake -S "$src" -B "$tmp/kdever" -DLT_SHELL=kde >"$tmp/kdever.log" 2>&1; then
    echo "note: KDE shell not configurable here (KF6 missing), skipping version check"
else
    grep -q 'LT_VERSION' "$tmp/kdever/compile_commands.json" 2>/dev/null \
        || fail "the KDE shell has no LT_VERSION - 'traduku --version' prints nothing"
    grep -q 'LT_TRAY_DEFAULT' "$tmp/kdever/compile_commands.json" 2>/dev/null \
        || fail "the KDE shell has no LT_TRAY_DEFAULT"
fi

echo "ok"
