#!/usr/bin/env bash
set -uo pipefail

# takes the cheri binary, a code's explanation in docs/errors, and a directory to work in, and checks the explanation's wrong program reports exactly that code and its fixed program checks clean
CHERI="$1"
DOC="$2"
WORK="$3"
CODE="$(basename "$DOC" .md)"
mkdir -p "$WORK"

# a retired code has no programs left to check, and neither does a rule no program written out can show, which says why under its own heading
if head -1 "$DOC" | grep -q "(retired)" || grep -qx "## No Example" "$DOC"; then
    exit 0
fi

# the info after a block's opening fence: toya to check it as a file of tests, whole to leave it without a start added
INFO="$(awk '/^## Wrong/ { found = 1 } found && /^```cherimoya/ { print; exit }' "$DOC")"

# writes the first code block under a heading to a file, as a .toya when the block says toya, and gives a program without one a start, so the example only shows what it's about
extract() {
    local heading="$1" out="$2"
    awk -v heading="## $heading" '
        $0 == heading { found = 1; next }
        found && /^```cherimoya/ { inside = 1; next }
        inside && /^```/ { exit }
        inside { print }
    ' "$DOC" > "$out"
    if [ ! -s "$out" ]; then
        echo "$DOC: no cherimoya block under '## $heading'"
        exit 1
    fi
    if [[ "$out" == *.moya ]] && [[ "$INFO" != *whole* ]] && ! grep -q -- "-> start ->" "$out"; then
        echo 'let () -> start -> (code :: int) { 0 -| }' >> "$out"
    fi
}

# a block marked toya is checked as a file of tests
EXTENSION=moya
if [[ "$INFO" == *toya* ]]; then
    EXTENSION=toya
fi

# a lexer or parser rule is about how a program is written, so only what those stages report counts, and its fixed program only has to be written right, even when it uses a feature sema doesn't support yet
STAGES='[A-Z]'
if [[ "$CODE" == L* || "$CODE" == P* ]]; then
    STAGES='[LP]'
fi
WRONG="$WORK/$CODE.wrong.$EXTENSION"
FIXED="$WORK/$CODE.fixed.$EXTENSION"
extract Wrong "$WRONG"
extract Fixed "$FIXED"

# the wrong program has to report this code and no other
OUTPUT="$("$CHERI" check "$WRONG" 2>&1)"
FOUND="$(grep -oE "^(error|warning)\[$STAGES[0-9]+\]" <<< "$OUTPUT" | sed -E 's/.*\[(.*)\]/\1/' | sort -u)"
if [ "$FOUND" != "$CODE" ]; then
    echo "$DOC: the wrong program reported '${FOUND//$'\n'/ }' instead of exactly $CODE:"
    echo "$OUTPUT"
    exit 1
fi

# the fixed program has to check with nothing to say
OUTPUT="$("$CHERI" check "$FIXED" 2>&1)"
STATUS=$?
if [[ "$STAGES" == '[LP]' ]]; then
    STATUS=0
fi
if [ $STATUS -ne 0 ] || grep -qE "^(error|warning)\[$STAGES" <<< "$OUTPUT"; then
    echo "$DOC: the fixed program didn't check clean:"
    echo "$OUTPUT"
    exit 1
fi
