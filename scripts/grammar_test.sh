#!/usr/bin/env bash
set -uo pipefail

# parses every .moya and .toya file under tests with the tree-sitter grammar in tree-sitter, since each one is a valid program, and fails naming every file whose tree holds an ERROR or MISSING node, then runs the grammar's corpus tests, where everything is skipped when tree-sitter isn't installed there, since it comes from npm and not from cmake
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GRAMMAR="$ROOT/tree-sitter"
TREE_SITTER="$GRAMMAR/node_modules/.bin/tree-sitter"
if [ ! -x "$TREE_SITTER" ]; then
    echo "skipped the grammar tests, since tree-sitter/node_modules isn't installed, please run 'npm install' in tree-sitter to run them"
    exit 0
fi

# tree-sitter warns when its config names no grammar directories, so it gets a config of its own naming an empty one, which is removed along with the list of files when the script exits
SCRATCH="$(mktemp -d)"
trap 'rm -rf "$SCRATCH"' EXIT
printf '{"parser-directories": ["%s"]}\n' "$SCRATCH" > "$SCRATCH/config.json"

# parses every program in one run, which builds the grammar first, where --quiet prints only the files that didn't parse cleanly, each with the first ERROR or MISSING node in it
find "$ROOT/tests" \( -name '*.moya' -o -name '*.toya' \) -type f | sort > "$SCRATCH/files"
if [ ! -s "$SCRATCH/files" ]; then
    echo "found no .moya or .toya files under tests"
    exit 1
fi
(cd "$GRAMMAR" && "$TREE_SITTER" parse --grammar-path . --config-path "$SCRATCH/config.json" --quiet --paths "$SCRATCH/files") > "$SCRATCH/parsed" 2>&1
STATUS=$?
FAILED="$(grep -E '\((ERROR|MISSING)' "$SCRATCH/parsed" | cut -f 1 | sed 's/[[:space:]]*$//')"
if [ -n "$FAILED" ]; then
    echo "the grammar couldn't parse these programs, which cheri reads without an error:"
    echo "$FAILED"
    echo "where the first ERROR or MISSING node in each one is:"
    grep -E '\((ERROR|MISSING)' "$SCRATCH/parsed"
    exit 1
fi
# a failure with no tree to show is tree-sitter itself failing, like a grammar that doesn't build
if [ "$STATUS" -ne 0 ]; then
    echo "tree-sitter couldn't parse the programs under tests:"
    cat "$SCRATCH/parsed"
    exit 1
fi
echo "the grammar parsed all $(wc -l < "$SCRATCH/files") programs under tests"

# runs the corpus tests, every construct with the tree it should have and every mistake cheri reports, from inside the grammar, which is where tree-sitter finds them
if ! (cd "$GRAMMAR" && "$TREE_SITTER" test --config-path "$SCRATCH/config.json"); then
    echo "the grammar's corpus tests failed, as the diff above shows"
    exit 1
fi
