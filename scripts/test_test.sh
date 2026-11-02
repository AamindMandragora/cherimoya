#!/usr/bin/env bash
set -uo pipefail

# takes the cheri binary, a directory of .toya files, and the output cheri test should print for it, then runs cheri test on the directory from where its path is relative, and checks that it fails, since the directory holds failing tests on purpose, and prints exactly that output
CHERI="$1"
DIR="$2"
EXPECTED="$3"

ACTUAL="$("$CHERI" test "$DIR")"
STATUS=$?
if [ "$STATUS" -ne 1 ]; then
    echo "expected cheri test to fail with 1, since some tests fail on purpose, got $STATUS"
    exit 1
fi
if ! diff <(cat "$EXPECTED") <(printf '%s\n' "$ACTUAL"); then
    echo "cheri test printed something other than $EXPECTED, as the diff shows"
    exit 1
fi
