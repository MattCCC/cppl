#!/usr/bin/env bash
# What a termination measure cannot let through (SPEC.md 22, 23, 24.3).
#
# A `decreases` clause makes termination part of what is verified. Every
# continuing iteration and every recursive call must make the measure strictly
# smaller in a well-founded order; recursion needs one; and a function that asks
# to terminate may run no loop and call no function whose termination is not
# established. Each program here breaks one of those and must be refused for
# that reason. What is accepted is in `fixtures/termination.cpp`, driven by
# `e2e/termination.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/termination.XXXXXX")

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

# SPEC: TERMINATION-007, TERMINATION-001
# Recursion needs a measure, in every function of it, all of one length.
refuse termination_recursion_without_measure \
    "termination_recursion_without_measure.cpp:8:12: error [proof-failure]: the termination of verified function 'forever' is not established: it calls itself and states no measure"
refuse termination_mutual_without_measure \
    "termination_mutual_without_measure.cpp:22:12: error [proof-failure]: the termination of verified function 'odd_steps' is not established: it calls 'even_steps', which reaches it again, and it states no measure"
refuse termination_mismatched_measures \
    "termination_mismatched_measures.cpp:18:5: error [proof-failure]: the termination of verified function 'right' is not established: its measure has 1 component, and that of 'left', which it recurses with, has 2 components"

# SPEC: TERMINATION-005, TERMINATION-006
# A recursive call at the same, a larger, a wrapped or an unknown measure does
# not descend, and the diagnostic names both measures.
refuse termination_same_measure \
    "termination_same_measure.cpp:9:12: error [kernel-rejection]: recursive call 'forever -> forever' is not shown to be made at a smaller measure than its caller was entered with" \
    "note: 'forever' was entered at measure (n); 'forever' is called with arguments (n), at its measure (n)"
refuse termination_larger_measure \
    "termination_larger_measure.cpp:12:12: error [kernel-rejection]: recursive call 'climbs -> climbs' is not shown to be made at a smaller measure"
refuse termination_wrapping_measure \
    "termination_wrapping_measure.cpp:9:12: error [kernel-rejection]: recursive call 'down -> down' is not shown to be made at a smaller measure"
refuse termination_unknown_measure \
    "termination_unknown_measure.cpp:17:12: error [kernel-rejection]: recursive call 'chase -> chase' is not shown to be made at a smaller measure"

# SPEC: TERMINATION-007
# A cycle through two functions descends on every edge, not just round trips.
refuse termination_mutual_cycle \
    "termination_mutual_cycle.cpp:14:12: error [kernel-rejection]: recursive call 'ping -> pong' is not shown to be made at a smaller measure"

# SPEC: TERMINATION-007
# A recursion is established whole or not at all: a member whose proof supposed
# a false member's contract is not established, nor is anything resting on it.
refuse termination_group_member_false \
    "termination_group_member_false.cpp:23:16: error [kernel-rejection]: return path 'twin path 1' does not satisfy its contract" \
    "termination_group_member_false.cpp:31:12: error [proof-failure]: return path 'outside path 1' does not satisfy its contract" \
    "callee 'down' is not proven"

# SPEC: TERMINATION-005
# A signed measure has no least element.
refuse termination_signed_measure \
    "termination_signed_measure.cpp:6:5: error [unsupported-semantics]: verified function 'count' cannot be stated to the formal core: a function measure must range over a well-founded domain, so its type must be unsigned"

# SPEC: TERMINATION-006, CORRECT-004
# A function that asks to terminate runs no loop without a measure, calls no
# function that may not terminate, and passes through no unsafe block.
refuse termination_unmeasured_loop \
    "termination_unmeasured_loop.cpp:9:5: error [proof-failure]: the termination of verified function 'spin' is not established: the loop at"
refuse termination_partial_callee \
    "termination_partial_callee.cpp:19:5: error [proof-failure]: the termination of verified function 'uses' is not established: it calls 'spin', whose termination is not established"
refuse termination_unsafe_block \
    "error [proof-failure]: the termination of verified function 'polls' is not established: it passes through the unsafe block at"

# SPEC: TERMINATION-007, CORRECT-003
# Descent justifies supposing the contract at a recursive call, not the
# contract: a false base case is still false.
refuse termination_false_base_case \
    "termination_false_base_case.cpp:10:16: error [kernel-rejection]: return path 'one path 1' does not satisfy its contract"

# SPEC: TERMINATION-005, LOOP-006
# Loops: a lexicographic list whose second part grows while the first stays, a
# `continue` path that skips the step, and a `do` loop's entry and exit.
refuse termination_lexicographic_loop \
    "termination_lexicographic_loop.cpp:12:20: error [kernel-rejection]: loop measure 'grows loop at line 10 measure' is not shown to decrease on every iteration" \
    "note: the measure is (r#2, c#3) at the head of the iteration, and is read again where this path ends it, at r = r#2, c = c#4"
refuse termination_continue_path \
    "termination_continue_path.cpp:10:20: error [kernel-rejection]: loop measure 'skips loop at line 8 measure' is not shown to decrease on every iteration"
refuse termination_do_loop_entry \
    "termination_do_loop_entry.cpp:9:20: error [kernel-rejection]: loop invariant 'first_pass loop at line 8 invariant 1' does not hold on entry"
refuse termination_do_loop_exit \
    "termination_do_loop_exit.cpp:15:12: error [kernel-rejection]: return path 'last path 1' does not satisfy its contract"

# SPEC: TERMINATION-002, CORRECT-005
# A recursive function is never a definition the formal core unfolds.
refuse termination_recursive_definition \
    "termination_recursive_definition.cpp:16:13: error [unsupported-semantics]: law 'unfolds' cannot be stated to the formal core: 'down' is not available to the formal core as a definition" \
    "'down' was not admitted because its body is not a single return expression"

# SPEC: TERMINATION-006
refuse termination_template_measure \
    "termination_template_measure.cpp:9:5: error [unsupported-semantics]: a 'decreases' clause on function template 'bounded' is not verified by this implementation"

echo 'no termination claim is accepted without its descent, and none is dropped'
