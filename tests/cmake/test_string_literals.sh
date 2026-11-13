#!/usr/bin/env bash
# Gate: no byte escapes inside a UTF-16 string literal, and no mojibake in the
# sources.
#
# Why this is a build gate and not a code-review habit. The tray label was
# written QStringLiteral("Traduku %1 \xC2\xB7 tray %2") - a UTF-8 byte sequence
# pasted into a literal the compiler reads as UTF-16, one QChar per byte. The
# source *looks* right (the bytes spell a middle dot), it compiles, it runs, and
# the user sees "Traduku 0.3.0 ? tray qt" on their own desktop. Nothing in a
# unit test reading the same string sees a problem, because the string is
# consistently wrong. Only the person looking at the screen finds out.
#
# Two rules, both mechanical:
#   1. a \xNN byte escape inside QStringLiteral(...) is always a mistake - the
#      literal's alphabet is UTF-16, so the spelling is \uXXXX. A doubled
#      \\x is different: that is text handed to a regex engine, and the
#      sentence splitter in core/chunker.cpp legitimately needs it.
#   2. no "A with circumflex" (U+00C2) anywhere in the sources: that character
#      has no business in this product's strings, and it is what the first byte
#      of a double-encoded UTF-8 sequence looks like after it survives.
#
# Both rules scan CODE, with comments removed. The first version of this gate
# failed on the comment that explains the bug - AppFlavour.h quotes the "A with
# circumflex" it is describing, which is the only way a comment can name a
# character. A gate that punishes the explanation of an incident trains people
# to delete explanations, and the next tray label is written by someone who has
# not read one.
#
# src/ and tests/ are both scanned: the same class of error reached the test for
# the tray line, where a "…" was written double-encoded, so the test asserted
# three characters of mojibake and went green against input it never matched.
#
# Usage: test_string_literals.sh <source dir>
set -eu

src="${1:-.}"
cd "$src"

fail=0

# Comments out, newlines kept: grep -n on the result reports the line a human
# has to edit. The alternation tries a string literal first at every position,
# so the "//" inside "https://…" is not mistaken for the start of a comment.
strip_comments() {
    perl -0777 -pe '
        s{ (":(?:\\.|[^"\\\n])*") | //[^\n]* | /\*.*?\*/ }
         { defined $1 ? $1 : ( "\n" x ( $& =~ tr/\n// ) ) }gex'
}

scan() {   # scan <regexp> <label> - greps the comment-free code of every source
    local rule="$1" label="$2" hits="" f tmp
    tmp="$(mktemp)"
    for f in $(find src tests -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.ui' \) 2>/dev/null); do
        strip_comments < "$f" > "$tmp"
        grep -nE "$rule" "$tmp" | sed "s|^|$f:|" || true
    done > "$tmp.hits"
    hits="$(cat "$tmp.hits")"
    rm -f "$tmp" "$tmp.hits"
    if [ -n "$hits" ]; then
        echo "FAIL: $label"
        printf '%s\n' "$hits" | sed 's/^/      /'
        fail=1
    fi
}

# 1. byte escapes in UTF-16 literals. A leading backslash is excluded so an
#    escaped backslash ("\\x26" - the regex text chunker.cpp needs) is not a hit.
scan 'QStringLiteral\("[^"]*[^\\]\\x[0-9A-Fa-f]{2}' \
     'a \xNN byte escape inside QStringLiteral() - that literal is UTF-16, so
       write \uXXXX (or the character itself)'

# 2. the symptom of a double-encoded string, spelled out as a code point so
# this script does not depend on the encoding it is itself read in.
acirc=$(printf '\303\202')     # U+00C2 in UTF-8
scan "$acirc" \
     'U+00C2 in a source file - that is what the first byte of a
       double-encoded UTF-8 sequence looks like'

if [ "$fail" = 0 ]; then
    echo "ok    string literals are spelled in one alphabet"
fi
exit "$fail"
