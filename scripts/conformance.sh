#!/usr/bin/env bash
# Runs the C++ source-compatibility suite: valid supported C++ stays valid
# C++L, in every supported target standard.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

ctest --preset "${CPPL_PRESET:-dev}" --tests-regex '^conformance_' "$@"
