#!/usr/bin/env bash
set -euo pipefail

# gets project and build directories
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"

# deletes build if exists
if [ -d "$BUILD" ]; then
    echo ">> removing $BUILD"
    rm -rf "$BUILD"
else
    echo ">> nothing to clean ($BUILD does not exist)"
fi