#!/usr/bin/env bash
# A condition or a validation establishes a refinement only on the path where
# it held (SPEC.md 28, RUNTIMECHECK-002 to RUNTIMECHECK-021, EDGECASE-079).
#
# Each program here moves a value into a refined type where nothing on its path
# establishes the predicate: on the failure path, before the test, after a
# write, call or unsafe block that replaced the tested version, through a route
# a disjunction or a failed conjunction selects, after a loop's condition
# stopped holding, or for another value than the one tested. Each is refused for
# that crossing and never made a runtime validation site (RUNTIMECHECK-013), and
# no program is produced. So is a validation expression the verifier cannot
# check, and one that is ordinary C++ because the unit gives `validate` a
# meaning of its own. The accepted twin of each is in
# `fixtures/runtime_validation.cpp`, driven by `e2e/runtime_validation.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/runtime-validation.XXXXXX")

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
    if grep -qE "PROVEN|RUNTIME-CHECKED|C\+\+L Trust Report" "$run/$name.log"; then
        echo "$name was described as established" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if ! grep -qF "$diagnostic" "$run/$name.log"; then
        echo "$name did not fail for the stated reason" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# SPEC: RUNTIMECHECK-007, EDGECASE-079 -- the failure path constructs nothing.
refuse runtime_check_failure_path \
    "runtime_check_failure_path.cpp:12:20: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Percentage'"
refuse runtime_check_result_unchecked \
    "runtime_check_result_unchecked.cpp:10:12: error [proof-failure]: return path 'unchecked_result path 2' does not satisfy its contract"
refuse runtime_check_argument_unchecked \
    "runtime_check_argument_unchecked.cpp:18:12: error [proof-failure]: call-site precondition for 'unchecked_argument -> positive_identity' is not proven"
refuse runtime_check_member_unchecked \
    "runtime_check_member_unchecked.cpp:16:25: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"
refuse runtime_check_element_unchecked \
    "runtime_check_element_unchecked.cpp:15:22: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"

# SPEC: RUNTIMECHECK-002, RUNTIMECHECK-004, RUNTIMECHECK-005 -- the check must
# establish the predicate, of the value entering, where it enters.
refuse runtime_check_after_crossing \
    "runtime_check_after_crossing.cpp:9:18: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"
refuse runtime_check_too_weak \
    "runtime_check_too_weak.cpp:12:18: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"
refuse runtime_check_other_value \
    "runtime_check_other_value.cpp:12:18: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"

# SPEC: RUNTIMECHECK-004 -- a fact belongs to the version checked, and a
# write, a verified call's effect or an unsafe block gives the local another.
refuse runtime_check_stale_after_write \
    "runtime_check_stale_after_write.cpp:15:18: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"
refuse runtime_check_stale_after_call \
    "runtime_check_stale_after_call.cpp:23:18: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"
refuse runtime_check_stale_after_unsafe \
    "runtime_check_stale_after_unsafe.cpp:18:18: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"

# SPEC: RUNTIMECHECK-002, INTERACT-022 -- short-circuit routes suppose only
# what C++ evaluated on them.
refuse runtime_check_or_route \
    "runtime_check_or_route.cpp:10:22: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"
refuse runtime_check_and_false_route \
    "runtime_check_and_false_route.cpp:13:18: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"

# SPEC: RUNTIMECHECK-010 -- after a loop, its condition's failure is what holds.
refuse runtime_check_loop_exit \
    "runtime_check_loop_exit.cpp:16:15: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Small'"

# SPEC: RUNTIMECHECK-007, RUNTIMECHECK-011, RUNTIMECHECK-013 -- a validation
# supposes nothing where it failed, and a crossing it did not establish is
# refused, never made a site.
refuse validation_failure_path \
    "validation_failure_path.cpp:13:20: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Percentage'"
# SPEC: RUNTIMECHECK-012 -- a validation fact belongs to the version tested.
refuse validation_stale_after_write \
    "validation_stale_after_write.cpp:14:18: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"
refuse validation_other_value \
    "validation_other_value.cpp:11:22: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"
refuse validation_wrong_refinement \
    "validation_wrong_refinement.cpp:12:24: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Percentage'"
refuse validation_or_route \
    "validation_or_route.cpp:11:22: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Positive'"

# SPEC: RUNTIMECHECK-018 to RUNTIMECHECK-020, WORD-013 -- what a validation
# expression may be.
refuse validation_outside_verified \
    "validation_outside_verified.cpp:6:12: error [unsupported-semantics]: a validation expression is checked only in the body of a verified function"
refuse validation_in_contract \
    "validation_in_contract.cpp:7:14: error [unsupported-semantics]: a validation expression is checked only in the body of a verified function"
refuse validation_in_invariant \
    "validation_in_invariant.cpp:12:20: error [unsupported-semantics]: a loop clause states a proposition, and a validation expression is runtime code"
refuse validation_in_unsafe \
    "validation_in_unsafe.cpp:11:13: error [unsupported-semantics]: a validation expression inside an unsafe block would not be checked"
refuse validation_unknown_refinement \
    "validation_unknown_refinement.cpp:9:9: error [unsupported-semantics]: 'Count' does not name a refinement type this translation unit declares"
refuse validation_qualified_name \
    "validation_qualified_name.cpp:11:9: error [unsupported-semantics]: a validation names the refinement type it tests by the name its declaration gives it"
refuse validation_indexed \
    "validation_indexed.cpp:8:9: error [unsupported-semantics]: validating a value against the indexed refinement type 'Index' is not supported"
refuse validation_formal_predicate \
    "validation_formal_predicate.cpp:4:20: error [unsupported-semantics]: refinement type 'Bounded' states a formal predicate, which no validation can evaluate at run time"
refuse validation_undefined_predicate \
    "refinement type 'Below' cannot be validated at run time: its predicate evaluates an operation C++ defines only under a condition on its operands"
refuse validation_layered \
    "validation_layered.cpp:11:9: error [unsupported-semantics]: verified function 'percentage_or_zero' cannot be stated to the formal core: refinement type 'Percentage' cannot be validated at run time: its base type is itself a refinement type"
refuse validation_word_in_use \
    "validation_word_in_use.cpp:15:9: warning [cppl-syntax]: 'validate' is also a name in this translation unit, so this expression is ordinary C++, not a validation"

echo 'every refinement nothing on its path established is refused, and never made a runtime validation site'
