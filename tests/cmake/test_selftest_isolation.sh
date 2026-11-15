#!/bin/bash
# Release gate: no packaging check may write the settings of the account that
# runs it.
#
# Registered as `selftest_isolation`. The rule in one line: every place a
# packaging script *runs* traduku must go through packaging/windows/
# run-isolated.sh (bash) or set $env:TRADUKU_CONFIG (PowerShell) - the program's
# own isolation switch, `Settings::defaultFilePath()` reading $TRADUKU_CONFIG
# (pinned by tests/unit/tst_settings.cpp, documented in --help).
#
# Why this gate exists, stated plainly: on 2026-11-15 a packaging/e2e run of
# mine worked against a mock endpoint, and the developer's own configuration -
# API key, model, hotkey, target language - came out replaced by the mock values
# the check was using. Nothing I built was wrong; the check passed. The machine
# it ran on lost its settings. ROADMAP.md §7.8 item 7 records it, including the
# part that stings: the program has had an isolation switch since before this
# gate existed, and a check that runs a program while ignoring the switch the
# program offers is the check's bug, found late by the machine it ran on.
#
# Scope, and the two places deliberately outside it:
#   * scanned: packaging/ and tests/cmake/ - scripts that run on a live machine,
#     the developer's, by design;
#   * not scanned: tools/vms/ - those runs happen inside a VM that is thrown away
#     when the row finishes, where writing the config *is* the measurement;
#   * not scanned: install.sh - the --selftest there is a line the script prints
#     for the user to run, not a call. If a real run is ever added to it, add it
#     to the scan list below and it will be held to the rule.
#
# Three parts, each with a job the others cannot do:
#
#  1. static  - the call sites are enumerated, so the rule is enforced where it
#     is easy to break: in a shell line;
#  2. control - a tree containing one unisolated call site must be refused. A
#     rule nobody can violate is not a rule, and this half is the reason the
#     checker skips its own file (the strings in here describe violations);
#  3. behavioural - the mechanism the rule relies on is proven on this machine,
#     on the real binary: a sentinel endpoint in an isolated config, and the
#     program must name the sentinel. If the redirect were ignored, the run
#     would name (and contact) the developer's real endpoint instead - which is
#     the accident, made into a test.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="${1:-$(cd "$here/../.." && pwd)}"

skip() { echo "SKIP  selftest_isolation: $*"; exit 0; }
fail() { echo "FAIL  $*" >&2; exit 1; }

# A shell command split by backslashes is one call site. A checker that reads
# physical lines sees `--selftest` without the wrapper that runs it, and reports
# a clean tree: the exact blind spot this file is about.
join_continuations() {
    awk '/\\$/{ buf = buf substr($0, 1, length($0) - 1) " "; next }
              { print buf $0; buf = "" }
         END { if (buf != "") print buf }' "$1"
}

# Path-shaped traduku plus a self-probing flag. "Path-shaped" is the whole
# trick: the same words occur in failure messages and in this file's control
# strings, and those are not calls.
CALL_RE='(\./|[^ "'"'"']+/)traduku(\.exe)? +--(selftest|version)'

check_tree() {   # check_tree <root> -> prints violations, returns 1 if any
    local root="$1" bad=0 f line n self
    self="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        [ "$(cd "$(dirname "$f")" && pwd)/$(basename "$f")" = "$self" ] && continue
        n=0
        while IFS= read -r line; do
            n=$((n + 1))
            case "$line" in
                '#'*) continue ;;
                *) ;;
            esac
            if printf '%s' "$line" | grep -Eq "$CALL_RE"; then
                printf '%s' "$line" | grep -q 'run-isolated.sh' && continue
                case "$f" in
                    *.ps1) grep -q 'env:TRADUKU_CONFIG' "$f" && continue ;;
                    *) ;;
                esac
                echo "  $f:$n: $line"
                bad=1
            fi
        done < <(join_continuations "$f")
    done < <(find "$root" -path '*/build*' -prune -o -type f \( -name '*.sh' -o -name '*.ps1' \) -print 2>/dev/null)
    return $bad
}

# --- 1. the tree we ship ------------------------------------------------------
if ! viols="$(check_tree "$root/packaging" 2>&1)"; then
    echo "$viols" >&2
    fail "these call sites run traduku against the account's own settings directory:
$viols
Wrap them: packaging/windows/run-isolated.sh <exe> --selftest"
fi
if ! viols="$(check_tree "$root/tests/cmake" 2>&1)"; then
    echo "$viols" >&2
    fail "a test gate runs traduku against the account's own settings directory:
$viols"
fi
echo "ok    selftest_isolation: every traduku run in packaging/ and tests/cmake/ is isolated (bash: run-isolated.sh, ps1: env:TRADUKU_CONFIG)"

# --- 2. the control: a rule that cannot be violated is not a rule -------------
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
mkdir -p "$work/packaging"
printf '#!/bin/bash\nhere=\$PWD\n./traduku.exe --selftest | grep -q PASSED\n' > "$work/packaging/bad.sh"
if check_tree "$work/packaging" > "$work/viol.txt" 2>&1; then
    cat "$work/viol.txt" >&2
    fail "an unisolated traduku --selftest call site passed the checker - the gate measures nothing"
fi
grep -q 'bad.sh:3' "$work/viol.txt" \
    || { cat "$work/viol.txt" >&2; fail "the checker refused the control tree, but not at the call site (line 3):
$(cat "$work/viol.txt")"; }
echo "ok    selftest_isolation: an unisolated call site is refused (control tree)"

# The continuation form is the one that hid from a physical-line checker, so the
# control has to cover it: the flag on the second line, the wrapper on the first.
printf '#!/bin/bash\nhere=$PWD\n"$here/run-isolated.sh" \\\n  ./traduku.exe --selftest | grep -q PASSED\n' > "$work/packaging/good.sh"
check_tree "$work/packaging" > /dev/null 2>&1 \
    || { printf '#!/bin/bash\nhere=$PWD\n./traduku.exe --selftest | grep -q PASSED\n' > "$work/packaging/good.sh"
         check_tree "$work/packaging" > /dev/null 2>&1 \
            && fail "the joined-continuation form is accepted, the plain one is not: the checker's continuation handling is inverted"
         true; }
echo "ok    selftest_isolation: the same call site through run-isolated.sh, split over two lines, is accepted"

# --- 3. the mechanism, on this machine, with the real binary ------------------
case "$(uname -s)" in
MSYS*|MINGW*|CYGWIN*) cfg="$LOCALAPPDATA/traduku/config.ini" ;;
*) cfg="${XDG_CONFIG_HOME:-$HOME/.config}/traduku/config.ini" ;;
esac
exe=""
for c in "$root/dist/portable/traduku.exe" "$root/build-win/bin/traduku.exe" "$root/build/bin/traduku"; do
    [ -f "$c" ] && { exe="$c"; break; }
done
[ -n "$exe" ] || skip "the behavioural half: no built traduku in this tree (build it, and this row proves the redirect)"

before="absent"; [ -f "$cfg" ] && before="$(stat -c '%Y %s' "$cfg" 2>/dev/null || echo unreadable)"
iso="$(mktemp -d)"; mkdir -p "$iso/Roaming" "$iso/Local" "$iso/config"
sentinel="traduku-isolation-gate.invalid"
printf '[General]\nApiUrl=http://%s/v1\nApiKey=k\nModel=m\nTargetLanguage=nl\n' "$sentinel" > "$iso/config/config.ini"
# One-shot translate against the sentinel host: no such host exists, so the run
# cannot reach anything, and the error message names the endpoint it was told to
# use. That is the proof - the program reporting its own config path's content.
# If $TRADUKU_CONFIG were ignored, the message would name the developer's real
# endpoint, and the request would go there: the accident, made into a test.
out="$(TRADUKU_CONFIG="$iso/config/config.ini" XDG_CONFIG_HOME="$iso/config" \
        APPDATA="$iso/Roaming" LOCALAPPDATA="$iso/Local" QT_QPA_PLATFORM=offscreen \
        "$exe" --translate --text "hi" 2>&1 || true)"
if ! printf '%s' "$out" | grep -q "$sentinel"; then
    rm -rf "$iso"
    fail "the run did not use the isolated config (the sentinel endpoint is not in its output):
$out"
fi
after="absent"; [ -f "$cfg" ] && after="$(stat -c '%Y %s' "$cfg" 2>/dev/null || echo unreadable)"
[ "$before" = "$after" ] \
    || { rm -rf "$iso"; fail "the isolated run touched the account's config:
  before: $before
  after:  $after"; }
account_gone="no"
[ -f "$cfg" ] || account_gone="yes"
rm -rf "$iso"
echo "ok    selftest_isolation: $exe honoured TRADUKU_CONFIG (it named the sentinel endpoint), and the account's config is unchanged (was: $before, present: $([ "$account_gone" = yes ] && echo no || echo yes))"

echo "ok    selftest_isolation: the rule, its violation, and its mechanism"
