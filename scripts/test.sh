#!/usr/bin/env bash
# Runs every test suite.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

ctest --preset "${CPPL_PRESET:-dev}" "$@"
