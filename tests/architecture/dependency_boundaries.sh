#!/usr/bin/env bash
# Dependency direction is an architectural invariant, not a convention.
#
# The kernel is the final authority on proof validity, so it must not depend on
# the frontend, the Clang bridge, the VIR, solvers, diagnostics rendering or
# editor tooling (ARCHITECTURE.md 56, 95; TRUST.md 5.4).
set -euo pipefail

KERNEL_SOURCE="$1"
KERNEL_LIBRARY="$2"

# 1. The kernel's sources include only their own headers and the standard library.
foreign_includes=$(grep -rhoE '#include[[:space:]]+"[^"]+"' "$KERNEL_SOURCE" \
    | grep -v 'cppl/kernel/' || true)
if [ -n "$foreign_includes" ]; then
    echo "the kernel includes headers from outside itself:" >&2
    echo "$foreign_includes" >&2
    exit 1
fi

forbidden=$(grep -rlE '#include[[:space:]]+<clang-c/|#include[[:space:]]+"cppl/(vir|frontend|clang|diagnostics|driver|obligations|automation|elaboration|erasure)/' \
    "$KERNEL_SOURCE" || true)
if [ -n "$forbidden" ]; then
    echo "the kernel includes a forbidden dependency:" >&2
    echo "$forbidden" >&2
    exit 1
fi

# 2. The built kernel resolves no symbol from any other C++L component.
if [ -f "$KERNEL_LIBRARY" ]; then
    undefined=$(nm -u "$KERNEL_LIBRARY" 2>/dev/null | grep -E 'ZN4cppl' | grep -vE 'ZN4cppl6kernel' || true)
    if [ -n "$undefined" ]; then
        echo "the kernel links against other C++L components:" >&2
        echo "$undefined" >&2
        exit 1
    fi
fi

echo "the kernel depends on nothing but itself and the standard library"
