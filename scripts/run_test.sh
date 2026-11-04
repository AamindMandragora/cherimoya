#!/usr/bin/env bash
set -uo pipefail

# takes the cheri binary, a .moya test whose first line is "# exit: N", and a directory to build into
CHERI="$1"
TEST="$2"
OUT="$3"
NAME="$(basename "$TEST" .moya)"

# reads the expected exit code from the first line, and the trap message a test that traps names on a "# trap: message" line
EXPECTED="$(head -n 1 "$TEST" | sed -n 's/^# exit: \([0-9]*\)$/\1/p')"
if [ -z "$EXPECTED" ]; then
    echo "$TEST: first line must be '# exit: N'"
    exit 1
fi
TRAP="$(sed -n 's/^# trap: //p' "$TEST" | head -n 1)"

# runs the program under valgrind when it's installed, so a leak, a double free, or a read of memory that was never written fails the test with valgrind's own exit code, which no test expects, where only memory nothing points to anymore counts as a leak, since a trap aborts with everything still in place
RUNNER=()
if command -v valgrind > /dev/null; then
    RUNNER=(valgrind --leak-check=full --errors-for-leak-kinds=definite --error-exitcode=213 -q)
fi

# builds and runs the test with no optimization and with the most, since the two can disagree, where a failed build fails the test
mkdir -p "$OUT"
for LEVEL in -O0 -O3; do
    if ! "$CHERI" build "$TEST" "$LEVEL" -o "$OUT/$NAME"; then
        echo "$TEST: build failed at $LEVEL"
        exit 1
    fi
    # runs it with a time limit, so a loop that never ends fails the test instead of hanging the run, and compares the exit code, which is only checked, never fatal, so set -e is off
    timeout 30 "${RUNNER[@]}" "$OUT/$NAME" 2> "$OUT/$NAME.stderr"
    ACTUAL=$?
    if [ "$ACTUAL" -eq 124 ]; then
        echo "$TEST: timed out after 30 seconds at $LEVEL"
        exit 1
    fi
    if [ "$ACTUAL" -ne "$EXPECTED" ]; then
        echo "$TEST: expected $EXPECTED at $LEVEL, got $ACTUAL"
        cat "$OUT/$NAME.stderr"
        exit 1
    fi
    # a test that traps has to trap for the reason it names, not just any reason
    if [ -n "$TRAP" ] && ! grep -qF -- "$TRAP" "$OUT/$NAME.stderr"; then
        echo "$TEST: expected the trap '$TRAP' at $LEVEL, got:"
        cat "$OUT/$NAME.stderr"
        exit 1
    fi
done
