#!/usr/bin/env bash
set -euo pipefail

# gets root and build directories
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"

# builds if necessary
"$ROOT/scripts/build.sh"

# runs all tests in the build directory and logs failures
echo ">> running tests"
ctest --test-dir "$BUILD" --output-on-failure "$@"
