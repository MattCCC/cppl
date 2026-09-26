#!/usr/bin/env bash
# Memory capabilities a verified call cannot pass on (SPEC.md 12.10
# VERIFIED-013, VERIFIED-037, VERIFIED-038, VERIFIED-043).
#
# A callee's `readable`/`writable` is owed at every call to it, exactly as its
# precondition is. Each program here calls a callee without holding what it
# requires, and each must be refused for that reason rather than proven. The
# accepted calls are in `fixtures/memory_capabilities.cpp`, driven by
# `e2e/memory_capabilities.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/memory_capabilities.XXXXXX")

refuse() {
    local name="$1"
    local diagnostic="$2"
    local status=0
    "$CPPL" -std=c++20 --cppl-trust-report "$FIXTURES/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1 ||
        status=$?

    if [ "$status" -eq 0 ]; then
        echo "$name was accepted" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if [ -e "$run/$name" ]; then
        echo "a program was produced for $name" >&2
        exit 1
    fi
    if grep -qE "PROVEN|C\+\+L Trust Report" "$run/$name.log"; then
        echo "$name was described as proven" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if ! grep -qF "$diagnostic" "$run/$name.log"; then
        echo "$name did not fail for the stated reason" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# SPEC: VERIFIED-043, VERIFIED-037
# Non-nullness establishes no capability, at a call as at a dereference.
refuse memory_capability_unstated \
    "calling 'touch' requires 'writable(q)', which is not established: the contract of 'caller' states no such capability"

# SPEC: VERIFIED-038
# `readable` does not entail `writable`.
refuse memory_capability_wrong_kind \
    "calling 'touch' requires 'writable(q)', which is not established: the contract of 'caller' states no such capability"

# SPEC: VERIFIED-043
# A capability for one pointer is not one for another.
refuse memory_capability_other_pointer \
    "calling 'touch' requires 'writable(other)', which is not established: the contract of 'caller' states no such capability"

# SPEC: VERIFIED-038
# The region a callee is handed may not be larger than the one the caller holds.
refuse memory_capability_region_exceeded "call-site precondition for 'caller -> fill' is not proven"
refuse memory_capability_empty_region "call-site precondition for 'caller -> touch' is not proven"

# SPEC: VERIFIED-038
# A place a read formed is written again only under `writable`, and one a
# write formed is read again only under `readable`.
refuse memory_capability_write_after_read \
    "writing through 'p' requires 'writable(p)', which was not established"
refuse memory_capability_read_after_write \
    "reading 'p' requires 'readable(p)', which was not established"

echo 'no verified call passes on a memory capability its caller does not hold'
