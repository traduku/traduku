#!/bin/sh
# Stage 6b: the LT_TRAY switch is a build-time guarantee, so it is tested at
# build-configuration time. A typo here (-DLT_TRAY=qtys) would otherwise
# configure happily and ship the KDE tray inside what everyone believes is
# the Qt-tray experiment - the exact mistake this stage exists to avoid.
#
# Usage: test_lt_tray_option.sh <source dir> <scratch dir>
set -u
src="$1"
tmp="$2"
fail() { echo "FAIL: $*" >&2; exit 1; }
have_cmake=0
command -v cmake >/dev/null 2>&1 || fail "cmake not found"

# 1. an invalid value must be a configure error that names the option.
out=$(cmake -S "$src" -B "$tmp/bad" -DLT_SHELL=gnome -DLT_TRAY=qyt 2>&1)
rc=$?
if [ "$rc" -eq 0 ]; then
    fail "configure accepted LT_TRAY=qyt (must be fatal)"
fi
echo "$out" | grep -qi 'LT_TRAY' \
    || fail "the fatal error does not mention LT_TRAY: $out"

# 2. both valid values configure (the GNOME shell ignores the value, but a
#    package must be able to pass either without a surprise).
for v in kde qt; do
    cmake -S "$src" -B "$tmp/$v" -DLT_SHELL=gnome -DLT_TRAY="$v" \
        >"$tmp/$v.log" 2>&1 \
        || fail "LT_TRAY=$v rejected: $(tail -5 "$tmp/$v.log")"
done

# 3. the value reaches the compiler, otherwise the shell could not honour it.
grep -rq 'LT_TRAY_DEFAULT' "$tmp/qt/compile_commands.json" 2>/dev/null \
    || fail "LT_TRAY does not reach the traduku target as LT_TRAY_DEFAULT"

echo "ok"
