#!/bin/bash
# Run a traduku binary so that it cannot touch the settings of the account that
# runs the build.
#
# Why this exists: on 2026-11-15 a packaging/e2e run of mine worked against a
# mock endpoint, and the developer's real %LOCALAPPDATA%\traduku\config.ini - API
# key, model, hotkey, target language - came out holding the mock's values. The
# check passed. The machine it ran on lost its configuration. ROADMAP.md §7.8
# item 7.
#
# The program already promises this: `Settings::defaultFilePath()` reads
# $TRADUKU_CONFIG (and `--config <file>` on the command line), pinned by
# tests/unit/tst_settings.cpp and named in --help. So this file is not a
# workaround invented by a test script - it is the documented isolation switch,
# and the bug was a check that ran the program while ignoring the switch the
# program offers.
#
# USAGE
#
#   "$BASH" packaging/windows/run-isolated.sh <exe> --selftest
#
# Not `packaging/windows/run-isolated.sh <exe> ...`, for two reasons that each
# cost a debugging round here, and both are properties of the checks that call
# it, not of this file:
#
#  1. the callers run with PATH narrowed to the system directory - that is the
#     point of those checks (no Qt, no compiler, only what the package carries)
#     - and under that PATH a shebang resolves to C:\Windows\System32\bash.exe,
#     which on Windows 11 is WSL's bash: it cannot see an MSYS path, answers "No
#     such file or directory", and the caller reports a package that does not
#     run. $BASH is this interpreter's own absolute path, found before PATH was
#     narrowed.
#  2. for the same reason this script may not call mktemp, mkdir, rm, cat or
#     grep: under a narrowed PATH they are not there. Everything below is a bash
#     builtin, a parameter expansion, or a redirect. The temp file goes into
#     $TMPDIR/$TEMP/$TMP - which Windows always sets - under a name unique to
#     this process; no directory is created, so no mkdir is needed; and the file
#     is left behind if `rm` is unavailable, in the temp directory, which is
#     where a throwaway belongs.
#
# What this does NOT isolate: anything the program writes through the shell
# rather than through Settings. `--autostart on` writes HKCU\...\Run, and no
# environment variable moves a registry key. That is the reason no packaging
# check here runs --autostart; the selftest's own autostart probe aims at a
# scratch location for the same reason (src/app/SelfTest.cpp).
#
# Usage: run-isolated.sh <program> [args...]
set -euo pipefail
[ $# -ge 1 ] || { echo "usage: $0 <program> [args...]" >&2; exit 2; }

base="${TMPDIR:-}"
[ -n "$base" ] || base="${TEMP:-}"
[ -n "$base" ] || base="${TMP:-}"
[ -n "$base" ] || base="."
case "$base" in */) ;; *) base="$base/" ;; esac   # ends with a slash, not "contains one"
cfg="${base}traduku-isolated-config-$$-$RANDOM.ini"

: > "$cfg"            # an existing file: first-run detection must not fire
export TRADUKU_CONFIG="$cfg"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

if command -v rm >/dev/null 2>&1; then
    trap 'rm -f "$cfg"' EXIT
else
    echo "run-isolated.sh: no rm on PATH; leaving the isolated config at $cfg" >&2
fi

exec "$@"
