#!/usr/bin/env bash
# Runs the suites that decide whether the proof system can be believed: the
# kernel's own rules, the programs that must be rejected, and the end-to-end
# Laws.
#
# A proof feature is never accepted on positive tests alone (AGENTS.md 24).
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

ctest --preset "${CPPL_PRESET:-dev}" \
    --tests-regex '^(kernel_|negative_|e2e_|architecture_)' "$@"
