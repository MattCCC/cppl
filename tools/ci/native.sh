#!/bin/sh
#
# Run a native CI preset the way GitHub Actions runs it.
#
# Usage:
#
#   tools/ci/native.sh <preset> [--dirty]
#
# Examples:
#
#   tools/ci/native.sh ci-macos-llvm
#   tools/ci/native.sh ci-asan
#
# This script holds no build policy. It resolves the toolchain the selected
# environment requires, clears ambient compiler flags, and then runs the
# commands the workflow's job for that preset runs (tools/ci/run-preset.sh):
# configure, build and test, or, for ci-quality, the repository checks, the
# formatting check and static analysis.
#
# Anything about how the project is compiled belongs in CMakePresets.json.

set -eu

preset="${1:-}"

if [ -z "${preset}" ]; then
    echo "usage: tools/ci/native.sh <preset> [--dirty]" >&2
    exit 2
fi

shift

clean=1

for argument in "$@"; do
    case "${argument}" in
        --dirty) clean=0 ;;
        *)
            echo "tools/ci/native.sh: unknown option '${argument}'" >&2
            exit 2
            ;;
    esac
done

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "${root}"

# --------------------------------------------------------------------------
# Toolchain
# --------------------------------------------------------------------------
#
# The ci-* presets read LLVM_ROOT. It is discovered here, never hard-coded,
# and an explicit LLVM_ROOT from the caller always wins -- that is how the
# GitHub runner supplies its own installation.

llvm_major=22

if [ -z "${LLVM_ROOT:-}" ]; then
    if command -v brew >/dev/null 2>&1; then
        LLVM_ROOT=$(brew --prefix "llvm@${llvm_major}" 2>/dev/null || true)
    fi

    if [ -z "${LLVM_ROOT:-}" ] && command -v "llvm-config-${llvm_major}" >/dev/null 2>&1; then
        LLVM_ROOT=$("llvm-config-${llvm_major}" --prefix)
    fi

    if [ -z "${LLVM_ROOT:-}" ] && [ -d "/usr/lib/llvm-${llvm_major}" ]; then
        LLVM_ROOT="/usr/lib/llvm-${llvm_major}"
    fi
fi

if [ -z "${LLVM_ROOT:-}" ] || [ ! -d "${LLVM_ROOT}" ]; then
    echo "tools/ci/native.sh: LLVM ${llvm_major} not found." >&2
    echo >&2
    echo "  macOS:  brew install llvm@${llvm_major}" >&2
    echo "  Linux:  see docker/ci/, or apt.llvm.org" >&2
    echo >&2
    echo "Or set LLVM_ROOT to an existing installation." >&2
    exit 1
fi

export LLVM_ROOT

# Named in the configure banner so a log says which preset produced it.
CPPL_CI_PRESET="${preset}"
export CPPL_CI_PRESET

# --------------------------------------------------------------------------
# Clean build tree
# --------------------------------------------------------------------------
#
# CI parity means configuring from nothing. A reused cache hides exactly the
# defects this is meant to catch: a stale compiler path, a dependency that is
# only found because a previous configure recorded it, a generated file that
# is no longer produced.

if [ "${clean}" -eq 1 ]; then
    # Every ci-<name> preset builds in build/ci/<name>; CMake does not expose
    # binaryDir to the command line, so the convention is relied on here and
    # asserted by the ci-preset-layout test.
    binary_dir="${root}/build/ci/${preset#ci-}"

    if [ -d "${binary_dir}" ]; then
        cmake -E rm -rf "${binary_dir}"
    fi
fi

# --------------------------------------------------------------------------
# Run
# --------------------------------------------------------------------------
#
# CFLAGS/CXXFLAGS/LDFLAGS are cleared rather than passed through. CMake bakes
# them into the cache on the first configure, so a developer shell that
# exports one produces a build the runner never performs. cmake/ci/
# HostEnvironment.cmake fails the configure if any survive.

exec env \
    -u CFLAGS \
    -u CXXFLAGS \
    -u CPPFLAGS \
    -u LDFLAGS \
    -u LIBRARY_PATH \
    -u CPATH \
    sh "${root}/tools/ci/run-preset.sh" "${preset}"
