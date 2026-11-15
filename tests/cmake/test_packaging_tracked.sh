#!/usr/bin/env bash
# Gate: every file under packaging/ is tracked.
#
# Why this is a test and not a habit. `packaging/windows/build-portable.sh` was
# written, run, reported "ok ... zip", and was never committed: .gitignore's
# `build-*` has no slash, so it matches at any depth and swallowed a *script*
# called build-portable.sh. Nothing downstream noticed - the release script
# existed on this machine, produced a valid archive, and a clean checkout would
# simply not have it. A file that git ignores is a file that does not exist for
# everyone else, and the failure looks exactly like success.
#
# The rule being enforced is narrow on purpose: packaging/ is where the release
# lives, so a build-* shaped name there is a script, not a build directory.
set -eu

repo="${1:-.}"
cd "$repo"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "SKIP packaging_tracked: not a git worktree"
    exit 0
fi

# The other half of the skip condition, and the second accident this gate has
# survived. Every git call below resolves paths against the ENCLOSING repo, so
# the index is evidence only for the worktree root. A release tarball extracted
# INSIDE the checkout - makepkg unpacks into release-out/src/traduku-$ver, a
# directory .gitignore keeps out of that index - passed the check above (it is
# inside a worktree), and the gate then reported every packaging file as "not
# tracked": true statements about paths under release-out/, false statements
# about a tree that was exactly HEAD. A gate that fails a clean release because
# of where the builder put it is measuring the builder, not the release.
top=$(git rev-parse --show-toplevel)
if [ "$(pwd -P)" != "$(cd "$top" && pwd -P)" ]; then
    echo "SKIP packaging_tracked: $(pwd -P) is not the worktree root ($top) - the git index describes the outer repo, not this tree"
    exit 0
fi

fail=0

# The release depends on these two by name; a rename that drops one is the same
# class of accident as an ignored file, so both are required to exist first.
for required in packaging/windows/build-portable.sh packaging/windows/deploy-runtime.sh; do
    if [ ! -f "$required" ]; then
        echo "FAIL $required is missing - the release cannot be built without it"
        fail=1
    fi
done

# Any file under packaging/ must be in the index. `git ls-files --others` lists
# the untracked ones; a directory ignored wholesale would hide its contents from
# that listing, so the file list itself comes from find, not from git.
while IFS= read -r f; do
    if git ls-files --error-unmatch -- "$f" >/dev/null 2>&1; then
        continue
    fi
    # Name the rule, not just the symptom: "untracked" invites the reader to
    # `git add` and move on, while the actual bug is a pattern two lines above
    # in .gitignore that was never meant to reach this deep.
    rule="$(git check-ignore -v "$f" 2>/dev/null || true)"
    echo "FAIL $f is not tracked"
    if [ -n "$rule" ]; then
        echo "     ignored by $rule"
        echo "     a build-directory pattern without a slash matches at any"
        echo "     depth; anchor it (/build-*) or move the script"
    else
        echo "     never added (git add $f)"
    fi
    fail=1
done < <(find packaging -type f \
             -not -name '*.orig' -not -name '*.bak' \
             -not -path '*/dist/*' -not -path '*/__pycache__/*' | sort)

if [ "$fail" = 0 ]; then
    count="$(find packaging -type f -not -path '*/dist/*' -not -path '*/__pycache__/*' | wc -l)"
    echo "ok    $count packaging files, all tracked"
fi
exit "$fail"
