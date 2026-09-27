#!/usr/bin/env bash
# A runtime check establishes a refinement only on the path where it held
# (SPEC.md 28, RUNTIMECHECK-002 to RUNTIMECHECK-007, EDGECASE-079).
#
# Each program here moves a value into a refined type where no check on its
# path establishes the predicate: on the failure path, before the check, after
# a write, call or unsafe block that replaced the checked version, through a
# route a disjunction or a failed conjunction selects, after a loop's condition
# stopped holding, or for another value than the one checked. Each is refused
# for that crossing, and no program is produced. The accepted twin of each is
# in `fixtures/runtime_validation.cpp`, driven by `e2e/runtime_validation.sh`.
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

echo 'every refinement a runtime check did not establish on its path is refused'
