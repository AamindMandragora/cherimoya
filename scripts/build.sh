#!/usr/bin/env bash
set -euo pipefail

# gets root and build directories, compile flags
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
CONFIG="${1:-Debug}"

# uses ninja if available, else makefiles
if command -v ninja >/dev/null 2>&1; then
    GEN="Ninja"
else
    GEN="Unix Makefiles"
fi

# if there's no build cache, prepare for compilation with clang
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
    echo ">> configuring ($GEN, $CONFIG)"
    cmake -S "$ROOT" -B "$BUILD" -G "$GEN" -DCMAKE_BUILD_TYPE="$CONFIG" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
fi

# compiles in parallel using previously generated build files
echo ">> building"
cmake --build "$BUILD" --parallel