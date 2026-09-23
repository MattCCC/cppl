#!/usr/bin/env bash
# Claims that a runtime path cannot occur which must be refused (SPEC.md
# VERIFIED-023, CASE-013, CASE-015).
#
# `contradiction evidence;` in a verified body is a claim, never an assumption.
# None of these establishes it, so none may produce a program. The one that
# matters most is the refused half of a matched pair whose accepted half is
# `after` in `fixtures/impossible_path.cpp`; the two differ only in the branch
# condition that leads to the claim.
set -euo pipefail

CPPL="$1"
# These fixtures exist only to be refused, so they live apart from the ones that
# must compile: nothing here is ever expected to produce a program.
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/impossible-paths.XXXXXX")

# Compiles a fixture that must fail, then checks it failed for the stated
# reason rather than for some unrelated mismatch an "it errored" test would
# also accept.
refuse() {
    local name="$1"
    local diagnostic="$2"
    local status=0
    "$CPPL" -std=c++17 "$FIXTURES/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1 || status=$?

    if [ "$status" -eq 0 ]; then
        echo "$name was accepted" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if [ -e "$run/$name" ]; then
        echo "a program was produced for $name" >&2
        exit 1
    fi
    if grep -q "PROVEN" "$run/$name.log"; then
        echo "$name was described as proven" >&2
        exit 1
    fi
    if ! grep -q "$diagnostic" "$run/$name.log"; then
        echo "$name did not fail for the stated reason" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# SPEC: VERIFIED-045, CASE-015
# The refused half of the matched pair, reported as the path's own claim, at the
# claim, and never mistaken for an omitted case.
refuse impossible_path_reachable "runtime path 'after path 1' is not shown to be unreachable"
grep -q "impossible_path_reachable.cpp:20:9" "$run/impossible_path_reachable.log"
if grep -q "omitted" "$run/impossible_path_reachable.log"; then
    echo 'a runtime path was reported as an omitted case' >&2
    exit 1
fi

# SPEC: VERIFIED-045, CASE-005
# A claim is about reachability, not about the goal. Every path here satisfies
# the postcondition, and the claim is still refused.
refuse impossible_path_under_a_provable_goal "runtime path 'anything path 1' is not shown to be unreachable"

# SPEC: VERIFIED-045, CASE-013
# The evidence is a proof the kernel admitted, instantiated at terms of the
# types it quantifies over, establishing an equality. Each path below is
# contradictory on its own, so only the evidence refuses it.
refuse impossible_path_unknown_proof "no proof named 'nowhere' is in scope here"
refuse impossible_path_argument_type "proof 'pinned' quantifies over 'u32' and cannot be instantiated at a term of type 'i32'"
refuse impossible_path_too_many_arguments "proof 'pinned' is instantiated at more arguments than it quantifies over"
refuse impossible_path_not_an_equality "'everything_equals_itself' does not establish an equality"
refuse impossible_path_refused_proof "proof 'false_claim' was not admitted"

# SPEC: VERIFIED-014, VERIFIED-045
# A callee's postcondition is a fact of the path only once it is proven, so a
# claim resting on an unproven one is not established.
refuse impossible_path_unproven_callee "runtime path 'uses_call path 1' is not shown to be unreachable"
grep -q "callee 'successor' is not proven" "$run/impossible_path_unproven_callee.log"

# SPEC: VERIFIED-045, WORD-011
# Nothing checks a claim in an ordinary function, so one is refused rather than
# erased unchecked.
refuse impossible_path_outside_verified "a claim that a path cannot occur is checked only in a verified function"

echo 'claims that a runtime path cannot occur are refused unless its facts genuinely contradict'
