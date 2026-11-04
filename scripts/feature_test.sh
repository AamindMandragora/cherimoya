#!/usr/bin/env bash
set -uo pipefail

# takes the cheri binary and a .toya file of feature tests, and runs its tests with no optimization and with the most, since the two can disagree, where any test that doesn't pass, or a file that doesn't build, fails the run
CHERI="$1"
TEST="$2"

# each test runs under valgrind when it's installed, so a leak, a double free, or a read of memory that was never written fails it too, where only memory nothing points to anymore counts as a leak, since a trap aborts with everything still in place
RUNNER=()
if command -v valgrind > /dev/null; then
    RUNNER=(--runner "valgrind --leak-check=full --errors-for-leak-kinds=definite --error-exitcode=213 -q")
fi

for LEVEL in -O0 -O3; do
    if ! "$CHERI" test "$TEST" "$LEVEL" "${RUNNER[@]}"; then
        echo "$TEST: some tests didn't pass at $LEVEL"
        exit 1
    fi
done
