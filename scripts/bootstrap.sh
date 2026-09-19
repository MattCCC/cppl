#!/usr/bin/env bash
# Configures the development build.
#
# Requires a Clang installation that provides libclang and a clang++ driver.
# Point CPPL_LLVM_ROOT at it when it is not on the default search paths.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

cmake --preset "${CPPL_PRESET:-dev}"
