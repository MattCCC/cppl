#!/usr/bin/env bash
# Decompositions each provider must refuse (SPEC.md 20). Every fixture here is
# the refused half of a matched pair whose accepted half verifies in
# `fixtures/enum_cases.cpp`, `fixtures/structural_cases.cpp` or
# `fixtures/expected_cases.cpp`, so a refusal shows the provider distinguishes
# the two rather than refusing everything nearby.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/case-providers.XXXXXX")

# Compiles a fixture that must fail, then checks it failed for the stated reason
# rather than for some unrelated mismatch an "it errored" test would also accept.
refuse() {
    local name="$1"
    local diagnostic="$2"
    local standard="${3:-c++20}"
    local status=0
    "$CPPL" "-std=$standard" "$FIXTURES/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1 || status=$?

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

# SPEC: CASE-002
# A false claim about an unsigned enumerator with its top bit set is refused
# because the kernel evaluates it to false, which it can only do if the
# enumerator arrived as the value its type holds.
refuse enum_top_bit_claimed_false "eq(4294967295, 4294967295) reduces to 1 while 0 reduces to 0"
! grep -q "malformed literal" "$run/enum_top_bit_claimed_false.log"

# SPEC: CASE-002
# A member enumeration of each class template instantiation is its own type, so
# a label resolved through another instantiation names no case here. It is
# refused by the provider, at that label, not by the parser.
refuse enum_label_of_another_instantiation "this label does not name a case of 'Machine<int>::Mode'"
grep -q "enum_label_of_another_instantiation.cpp:15:9" "$run/enum_label_of_another_instantiation.log"

# SPEC: CASE-007
# A declared but undefined class template has no specialization to instantiate,
# so it stays incomplete and is refused by the provider rather than by C++.
refuse decompose_undefined_template "proof decomposition unavailable for incomplete type"
grep -q "decompose_undefined_template.cpp:12:5" "$run/decompose_undefined_template.log"
! grep -q "cpp-semantic" "$run/decompose_undefined_template.log"

echo 'each decomposition provider refuses the half of its matched pairs that does not hold'
