#!/usr/bin/env bash
# Write the portable zip: the one place where the layout of a shipped archive
# is decided, so there is one thing to test.
#
#   usage: mkzip.sh <out.zip> <staged-dir>
#
# Why this is not a `tar` one-liner in build-portable.sh. Every 0.4.0 release
# check was green and the archive on the release page could not be unpacked:
# `bsdtar -a -cf x.zip -C stage .` stores every member as `./traduku.exe`, and
# Windows' own compressed-folder engine (zipfldr - what a double-click and
# "Extract All" use) refuses to open an archive whose entries start with `./`.
# The build script did not see it because it verified the archive with bsdtar,
# the tool that wrote it, and bsdtar accepts `./` silently. A package verified
# by the tool that produced it is verified by nobody: the layout has to be
# checked with the reader the user has (see check-portable-zip.sh).
#
# The rule the writer enforces: members are named exactly as they appear
# beside traduku.exe after extraction. No `./`, no absolute paths, no directory
# entry for the archive root.
set -euo pipefail

[ $# -eq 2 ] || { echo "usage: $0 <out.zip> <staged-dir>" >&2; exit 2; }
out="$1"; stage="$2"
[ -d "$stage" ] || { echo "mkzip.sh: no such directory: $stage" >&2; exit 2; }

# bsdtar is Windows' own tar (System32), named by path for the same reason the
# archive writer is named at all: git bash ships GNU tar, whose -a does not
# understand a .zip suffix and would produce a gzip'd tar named .zip - an
# archive that "built" and unpacks into nonsense.
bsdtar="/c/Windows/System32/tar.exe"
if [ ! -x "$bsdtar" ]; then bsdtar="$(command -v tar.exe || true)"; fi
[ -n "${bsdtar:-}" ] && [ -x "${bsdtar:-}" ] || bsdtar=""

# The release is packed on Windows with the reader's own vendor's writer. The
# gate that checks the reader rules also runs on Linux, where there is no
# Windows bsdtar; there the same member rules are applied by Python's zipfile
# (stdlib, deterministic) so the rules are exercised on both machines, and the
# Windows release path is the one users get the archive from.
if [ -z "$bsdtar" ] && ! command -v python >/dev/null 2>&1 && ! command -v python3 >/dev/null 2>&1; then
    echo "mkzip.sh: no bsdtar and no python to write the archive" >&2
    exit 1
fi

# Native Windows bsdtar does not read MSYS paths, so both ends are named in the
# form it expects: the output as an absolute Windows path (the caller's
# relative path would resolve against the stage directory once we cd), the
# members as plain relative names.
out_dir="$(dirname "$out")"
out_name="$(basename "$out")"
abs_out="$(cd "$out_dir" && pwd -W 2>/dev/null || pwd)"/$out_name
if [ -n "$bsdtar" ]; then abs_out="$(cygpath -w "$abs_out" 2>/dev/null || echo "$abs_out")"; fi

# Dotfiles would be silently dropped by the glob below, which is the same class
# of bug as the one this script exists to fix - a file that is on disk, not in
# the archive, and in nothing's way. The staged tree is built by us and has no
# dotfiles; say so in code.
if [ -n "$(find "$stage" -mindepth 1 -name '.*' -print -quit)" ]; then
    echo "mkzip.sh: $stage contains a dotfile; the member list below would drop it" >&2
    exit 1
fi

rm -f "$abs_out"
if [ -n "$bsdtar" ]; then
    # `cd` is what makes the names relative; -- ends the option list so a staged
    # file can never be read as a flag.
    (cd "$stage" && "$bsdtar" -a -cf "$abs_out" -- *) \
        || { echo "mkzip.sh: bsdtar refused to write $abs_out" >&2; exit 1; }
else
    py="$(command -v python || command -v python3)"
    "$py" - "$abs_out" "$stage" <<'PY' || { echo "mkzip.sh: refused to write the archive" >&2; exit 1; }
import os, sys, zipfile
out, stage = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(stage):
        dirs.sort(); files.sort()
        for f in files:
            p = os.path.join(root, f)
            # The name a user sees after extraction, spelled exactly so.
            z.write(p, os.path.relpath(p, stage).replace(os.sep, "/"))
PY
fi
[ -s "$abs_out" ] || { echo "mkzip.sh: wrote an empty archive" >&2; exit 1; }

# Self-check the writer's own contract, so a future flag change that reintroduces
# the prefix fails here, in the writer, not on the release page.
listing="$("${bsdtar:-}" -tf "$abs_out" 2>/dev/null || true)"
if [ -z "$listing" ]; then
    py="$(command -v python || command -v python3)"
    listing="$("$py" -c 'import sys,zipfile; print("\n".join(zipfile.ZipFile(sys.argv[1]).namelist()))' "$abs_out")"
fi
if printf '%s\n' "$listing" | grep -q '^\./'; then
    echo "mkzip.sh: the archive it just wrote has './' members - zipfldr (Explorer, Extract All) cannot open such an archive" >&2
    printf '%s\n' "$listing" | sed -n '1,3p' >&2
    exit 1
fi
count="$(find "$stage" -type f | wc -l | tr -d ' ')"
packed="$(printf '%s\n' "$listing" | grep -cv '/$' | tr -d ' ')"
[ "$count" = "$packed" ] || {
    echo "mkzip.sh: $count staged files, $packed files in the archive - the archive is not the tree" >&2
    exit 1
}

echo "ok    wrote $(basename "$abs_out"): $packed files, no ./ prefix"
