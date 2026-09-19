#!/usr/bin/env bash
# The gate: configure, build and run everything.
#
# Developers, agents and CI run this same script, so there is no separate CI
# build logic to drift from it (ARCHITECTURE.md 71).
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

./scripts/bootstrap.sh
./scripts/build.sh
./scripts/test.sh
