#!/usr/bin/env bash
# Builds the compiler and the test binaries.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

preset="${CPPL_PRESET:-dev}"
if [ ! -d "build/${preset}" ]; then
    cmake --preset "${preset}"
fi

cmake --build --preset "${preset}" "$@"
