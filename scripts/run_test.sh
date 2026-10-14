#!/usr/bin/env bash
set -uo pipefail

# takes the cheri binary, a .moya test whose first line is "# exit: N", and a directory to build into
CHERI="$1"
TEST="$2"
OUT="$3"
NAME="$(basename "$TEST" .moya)"

# reads the expected exit code from the first line
EXPECTED="$(head -n 1 "$TEST" | sed -n 's/^# exit: \([0-9]*\)$/\1/p')"
if [ -z "$EXPECTED" ]; then
    echo "$TEST: first line must be '# exit: N'"
    exit 1
fi

# builds the test, where a failed build fails the test
mkdir -p "$OUT"
if ! "$CHERI" build "$TEST" -o "$OUT/$NAME"; then
    echo "$TEST: build failed"
    exit 1
fi

# runs it with a time limit, so a loop that never ends fails the test instead of hanging the run, and compares the exit code, which is only checked, never fatal, so set -e is off
timeout 10 "$OUT/$NAME"
ACTUAL=$?
if [ "$ACTUAL" -eq 124 ]; then
    echo "$TEST: timed out after 10 seconds"
    exit 1
fi
if [ "$ACTUAL" -ne "$EXPECTED" ]; then
    echo "$TEST: expected $EXPECTED, got $ACTUAL"
    exit 1
fi
