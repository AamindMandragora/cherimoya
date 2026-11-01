#!/usr/bin/env bash
set -uo pipefail

# takes the cheri binary, a .moya debug test, a directory to build into, and llvm-dwarfdump, where the test builds with its "# flags:" line, -g -O0 by default, then every "# dwarf:" line has to appear in llvm-dwarfdump's view of the debug info, and every "# expect:" line in the output of one gdb session running its "# gdb:" lines, which is skipped when gdb isn't installed
CHERI="$1"
TEST="$2"
OUT="$3"
DWARFDUMP="$4"
NAME="$(basename "$TEST" .moya)"

# reads every line of one kind from the test, without its marker
lines() {
    sed -n "s/^# $1: //p" "$TEST"
}
FLAGS="$(lines flags | head -n 1)"
FLAGS="${FLAGS:--g -O0}"

# builds the test, where a failed build fails it
mkdir -p "$OUT"
# shellcheck disable=SC2086
if ! "$CHERI" build "$TEST" $FLAGS -o "$OUT/$NAME"; then
    echo "$TEST: build failed with $FLAGS"
    exit 1
fi

# checks the debug info itself, which needs nothing but llvm-dwarfdump
"$DWARFDUMP" --debug-info --debug-line "$OUT/$NAME" > "$OUT/$NAME.dwarf"
while IFS= read -r expected; do
    if ! grep -qF -- "$expected" "$OUT/$NAME.dwarf"; then
        echo "$TEST: expected the debug info to contain '$expected'"
        exit 1
    fi
done < <(lines dwarf)

# runs the gdb session, allowing the binary's embedded printers to load, and checks what it printed
if [ -z "$(lines gdb)" ]; then exit 0; fi
if ! command -v gdb > /dev/null; then
    echo "$TEST: gdb isn't installed, so only the debug info was checked"
    exit 0
fi
lines gdb > "$OUT/$NAME.gdb"
gdb -batch -nx -iex "set auto-load safe-path /" -x "$OUT/$NAME.gdb" "$OUT/$NAME" > "$OUT/$NAME.session" 2>&1
while IFS= read -r expected; do
    if ! grep -qF -- "$expected" "$OUT/$NAME.session"; then
        echo "$TEST: expected gdb to print '$expected', got:"
        cat "$OUT/$NAME.session"
        exit 1
    fi
done < <(lines expect)
