#!/usr/bin/env bash
# What an unsafe boundary cannot carry into verified code (SPEC.md 26; TRUST.md
# TCB-UNSAFE-001 to TCB-UNSAFE-003).
#
# An unsafe block establishes nothing: what it wrote is unknown after it, what it
# wrote keeps no refinement, no capability survives it, and control goes on
# after it. An unsafe function is neither verified nor pure and states no
# contract. Each program here tries to carry a fact across the boundary that
# nothing checked, and each must be refused for that reason rather than proven.
# What the boundary does allow is in `fixtures/unsafe_boundary.cpp`, driven by
# `e2e/unsafe_boundary.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/unsafe_boundary.XXXXXX")

refuse() {
    local name="$1"
    shift
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
    local diagnostic
    for diagnostic in "$@"; do
        if ! grep -qF "$diagnostic" "$run/$name.log"; then
            echo "$name did not fail for the stated reason: $diagnostic" >&2
            cat "$run/$name.log" >&2
            exit 1
        fi
    done
}

# SPEC: UNSAFE-003, UNSAFE-005
# A false postcondition: the block adds one, and nothing checked that it did.
refuse unsafe_false_postcondition \
    "unsafe_false_postcondition.cpp:12:12: error [kernel-rejection]: return path 'bumped path 1' does not satisfy its contract"

# SPEC: BOUNDARYEX-010
# What an unsafe call returned has no range, whatever the device does.
refuse unsafe_result_has_no_facts \
    "unsafe_result_has_no_facts.cpp:15:12: error [kernel-rejection]: return path 'always_in_range path 1' does not satisfy its contract"

# SPEC: UNSAFE-003
# A refinement smuggled across the block: the value stored there owes none.
refuse unsafe_refinement_smuggled \
    "unsafe_refinement_smuggled.cpp:14:12: error [kernel-rejection]: return path 'smuggled path 1' does not satisfy its contract"

# SPEC: UNSAFE-005
# A stale alias fact: an address kept by one block is written by a later one
# that never names it.
refuse unsafe_stale_alias \
    "unsafe_stale_alias.cpp:27:12: error [kernel-rejection]: return path 'stale path 1' does not satisfy its contract"

# SPEC: UNSAFE-005
# Storage a reference parameter designates is reachable from the block unnamed.
refuse unsafe_reference_parameter \
    "unsafe_reference_parameter.cpp:15:5: error [kernel-rejection]: return path 'through_reference path 1' does not satisfy its contract"

# SPEC: UNSAFE-005, INTERACT-018
# A loop counter the block writes keeps no invariant across it.
refuse unsafe_loop_counter \
    "unsafe_loop_counter.cpp:8:5: error [kernel-rejection]: loop invariant 'count loop at line 8 invariant 1' is not preserved by an iteration"

# SPEC: UNSAFE-005, VERIFIED-043
# Invalid pointer facts: a pointer the block may rebind is not followed, and no
# capability survives the block for a write or for a verified call.
refuse unsafe_pointer_rebound \
    "the unsafe block at" "unsafe_pointer_rebound.cpp:7 may change parameter 'p' itself, which this body does not track"
# A member written is the object it belongs to written, however the parameter
# is left untracked.
refuse unsafe_member_write \
    "the unsafe block at" "unsafe_member_write.cpp:22 may change parameter 's' itself, which this body does not track"
refuse unsafe_capability_revoked_write \
    "writing through 'q' requires 'writable(q)', which no longer holds after the unsafe block at" \
    "unsafe_capability_revoked_write.cpp:8: what that block did to the storage was not checked"
refuse unsafe_capability_revoked_call \
    "calling 'touch' requires 'writable(q)', which no longer holds after the unsafe block at" \
    "unsafe_capability_revoked_call.cpp:13: what that block did to the storage was not checked"
# A place read before the block is not readable after it on that account.
refuse unsafe_capability_revoked_read \
    "reading 'p' requires 'readable(p)', which no longer holds after the unsafe block at" \
    "unsafe_capability_revoked_read.cpp:12: what that block did to the storage was not checked"

# SPEC: UNSAFE-002
# A verified path calls an unsafe function only inside an unsafe block.
refuse unsafe_call_outside_block \
    "it calls unsafe function 'read_device' outside an unsafe block; a verified body crosses that boundary only inside one (SPEC.md UNSAFE-002)"

# SPEC: UNSAFE-003
# Control goes on after the block: a return, or a break of an enclosing loop,
# would continue from code nothing checked. A break of a loop inside the block
# stays inside it, so only the second break is reported.
refuse unsafe_block_return \
    "unsafe_block_return.cpp:7 through a return; a verified body passes through an unsafe block and goes on after it"
refuse unsafe_block_break \
    "unsafe_block_break.cpp:11 through a break; a verified body passes through an unsafe block and goes on after it"

# SPEC: UNSAFE-002
# `unsafe` never waives what `verified` or `pure` asks for, however the
# declarations are spelled.
refuse unsafe_verified_function \
    "unsafe_verified_function.cpp:3:1: error [cppl-syntax]: 'unsafe' cannot be combined with 'verified'"
refuse unsafe_redeclared_verified \
    "unsafe_redeclared_verified.cpp:5:19: error [cppl-syntax]: 'identity' is declared unsafe, so it cannot also be verified"
refuse unsafe_pure_function \
    "pure function 'pure_with_block' holds an unsafe block, whose effects are not checked, so it cannot be established pure" \
    "law 'returns_argument' cannot be stated to the formal core: 'pure_with_block' is not available to the formal core as a definition"

# SPEC: UNSAFE-003, UNSAFE-004
# Hidden assumptions: a proposition about what an unsafe function returns has no
# definition to rest on, and a contract on an unsafe function would be one.
refuse unsafe_in_proposition \
    "law 'device_answers' cannot be stated to the formal core: 'read_device' is not available to the formal core as a definition"
refuse unsafe_contract \
    "unsafe_contract.cpp:5:5: error [cppl-syntax]: an unsafe function states no contract"

# SPEC: UNSAFE-003
# Proof syntax inside a block would state what nothing checks.
refuse unsafe_loop_specification \
    "unsafe_loop_specification.cpp:9:9: error [unsupported-semantics]: a loop specification inside an unsafe block would not be checked"

# SPEC: UNSAFE-001
refuse unsafe_member_function \
    "unsafe_member_function.cpp:6:5: error [unsupported-semantics]: 'unsafe' is applied outside namespace scope"

echo 'no fact crosses an unsafe boundary into verified code unchecked'
