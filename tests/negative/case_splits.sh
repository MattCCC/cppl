#!/usr/bin/env bash
# Case splits on a runtime path that must be refused (SPEC.md 20.7). Most are the
# refused half of a matched pair whose accepted half verifies in
# `fixtures/case_split.cpp`, so a refusal shows the split tells the two apart
# rather than refusing everything nearby.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/case-splits.XXXXXX")

# Compiles a fixture that must fail, then checks it failed for the stated reason
# rather than for some unrelated mismatch an "it errored" test would also accept.
refuse() {
    local name="$1"
    local diagnostic="$2"
    local status=0
    "$CPPL" -std=c++20 "$FIXTURES/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1 || status=$?

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

# SPEC: CASE-018
# Without the split, the contract the split establishes is not established.
refuse split_needed_for_contract "verified function 'unsplit' does not satisfy its contract"

# SPEC: CASE-020
# A split reads the version current where it is written: after a write, an
# aliased write, a call, a write to a member or at a symbolic subscript, and at
# the head of a loop that writes it, an earlier fact describes nothing.
refuse split_stale_after_write \
    "omitted case 'Mode::busy' of verified function 'stale_after_write' is not shown to be impossible"
refuse split_alias_write "omitted case 'Mode::busy' of verified function 'alias_write' is not shown to be impossible"
refuse split_after_call "omitted case 'Mode::busy' of verified function 'after_call' is not shown to be impossible"
refuse split_member_write \
    "omitted case 'Mode::busy' of verified function 'member_write' is not shown to be impossible"
refuse split_symbolic_element "omitted case 'unnamed' of verified function 'elements' is not shown to be impossible"
refuse split_loop_carried \
    "omitted case 'Mode::busy' of verified function 'loop_carried' is not shown to be impossible"
# Refused inside the loop, not at the split before it, where the entry fact holds.
grep -q "split_loop_carried.cpp:34:" "$run/split_loop_carried.log"
! grep -q "split_loop_carried.cpp:22:" "$run/split_loop_carried.log"

# SPEC: CASE-017, CASE-018
# A binder is the value its case exposes, and an arm supposes its own case and
# nothing more.
refuse split_member_binder \
    "omitted case 'Mode::idle' of verified function 'member_binder' is not shown to be impossible"
refuse split_arm_reachable_claim "is not shown to be unreachable"
grep -q "split_arm_reachable_claim.cpp:23:13" "$run/split_arm_reachable_claim.log"
! grep -q "split_arm_reachable_claim.cpp:19:13" "$run/split_arm_reachable_claim.log"

# SPEC: CASE-004, CASE-002, CASE-003
# The arms are read by the one rule for arms, and a refusal of them is reported
# once, in that rule's words.
refuse split_non_exhaustive "non-exhaustive cases: 'Mode::busy' has no arm"
refuse split_wrong_label "this label does not name a case of 'Mode'"
refuse split_decompose_sum "decompose requires a product; use cases for alternative states"
for name in split_non_exhaustive split_wrong_label split_decompose_sum; do
    ! grep -q "cannot state as a value" "$run/$name.log"
done

# SPEC: CASE-017
# A binder read at a type its case does not bind is refused, not converted.
refuse split_template_binder_types "a case binder was resolved at type"

# SPEC: CASE-006
# A binder never repeats an enclosing value's name.
refuse split_binder_repeats_name "case binder 'value' duplicates an enclosing value name"

# SPEC: WORD-012, CASE-019
# Where a split may stand, and what may stand in its arms.
refuse split_outside_verified "a case split on a runtime path is checked only in a verified function"
refuse split_arm_closes_goal "'refl' has no goal to close in a case split on a runtime path"
refuse split_after_claim "nothing after a contradiction in this arm is reached"

echo 'case splits on runtime paths are refused wherever a fact would outlive its version or a state would lose its arm'
