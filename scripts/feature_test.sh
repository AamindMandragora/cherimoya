#!/usr/bin/env bash
set -uo pipefail

# takes the cheri binary and a .toya file of feature tests, and runs its tests with no optimization and with the most, since the two can disagree, where any test that doesn't pass, or a file that doesn't build, fails the run
CHERI="$1"
TEST="$2"

for LEVEL in -O0 -O3; do
    if ! "$CHERI" test "$TEST" "$LEVEL"; then
        echo "$TEST: some tests didn't pass at $LEVEL"
        exit 1
    fi
done
