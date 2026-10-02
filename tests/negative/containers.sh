#!/usr/bin/env bash
# The verified sequence subset refuses what it cannot state soundly (SPEC.md
# J.17, STDMODEL-010 to STDMODEL-022; RFC 0020).
#
# Each program here is refused for the stated reason, and each claim it makes
# would be false wherever the refused mechanism were missing: an index past the
# end, storage a view outlived, a value that breaks a refinement, a length the
# program no longer has. The accepted twin of each, differing in the one thing
# the refusal is about, is in `fixtures/containers.cpp`, driven by
# `e2e/containers.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

# shellcheck source=../support/parallel.sh
source "$(dirname "$0")/../support/parallel.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/containers.XXXXXX")

# Each refusal is a case of its own, in files of its own, so they run side by
# side (support/parallel.sh).
cases_begin "$run/cases"

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

# --- Bounds are proved, whatever the container (STDMODEL-011, STDMODEL-012) --

# SPEC: STDMODEL-011
case_run refuse container_array_out_of_bounds "law 'array_unguarded element index' is not proven"
# SPEC: STDMODEL-012
case_run refuse container_vector_out_of_bounds "law 'vector_unguarded element index' is not proven"
case_run refuse container_string_out_of_bounds "law 'string_unguarded element index' is not proven"
case_run refuse container_span_out_of_bounds "law 'span_unbounded element index' is not proven"
# SPEC: STDMODEL-015
# A bound proved before `pop_back` bounds nothing after it.
case_run refuse container_element_after_pop "law 'read_after_pop element index' is not proven"
# An element place formed before `pop_back` is not matched after it.
case_run refuse container_element_reused_after_pop "law 'reread_after_pop element index' is not proven"
# SPEC: ARITH-008
# A signed index is bounded as the size-type value C++ converts it to.
case_run refuse container_signed_index "law 'unguarded_sign element index' is not proven"
# SPEC: ARITH-009
# A length may be zero.
case_run refuse container_divide_by_length "division by zero: the divisor"
# The parser shape: a false bound, and an element read where no statement
# formed it.
case_run refuse container_parser_false_bound "does not satisfy its contract"
case_run refuse container_element_in_condition "this element access is not one the statement holding it formed"
# SPEC: STDMODEL-011
case_run refuse container_array_reference_parameter "an element of the std::array a reference designates is not modeled"

# --- Summaries owe their preconditions (STDMODEL-013, STDMODEL-023) ---------

# SPEC: STDMODEL-023
case_run refuse container_pop_empty "call-site precondition for 'pop_unguarded -> std::vector::pop_back' is not proven"
# A by-value argument is a copy; the caller's vector is not what the callee grew.
case_run refuse container_by_value_call "return path 'grown_by_callee path 1' does not satisfy its contract"

# --- A span holds no validity by existing (STDMODEL-016, STDMODEL-017) ------

# SPEC: STDMODEL-016
case_run refuse container_span_without_capability "reading an element of 'in' requires 'readable(in)', which was not established"
case_run refuse container_forward_without_capability "calling 'count' requires 'readable(in)', which is not established"
case_run refuse container_span_after_unsafe "requires 'readable(in)', which no longer holds after the unsafe block"
# SPEC: STDMODEL-016
# Only a verified function's `expects` reads a capability conjoined with a
# predicate apart; anywhere else the capability would be lost unread.
case_run refuse capability_conjoined_postcondition "the postcondition of verified function 'read_once' conjoins a memory capability with a predicate"
case_run refuse capability_conjoined_trusted_law "law 'readable_and_one' conjoins a memory capability with a predicate"
case_run refuse capability_conjoined_law_premise "the precondition of law 'positive_when_readable' conjoins a memory capability with a predicate"
case_run refuse container_self_view_call "by a reference through which the callee may reallocate it"
# SPEC: STDMODEL-017
case_run refuse container_data_overrun "call-site precondition for 'data_overrun -> clear_prefix' is not proven"
# One element written through a reference and through a view of its container
# in one call is refused at the source, naming both, never as an internal
# inconsistency of the formal statement.
case_run refuse container_element_and_data_call "'v[0]', an element of 'v', is passed to 'touch' by mutable reference, and the same call hands it a view or data pointer"
case_run refuse container_element_and_span_call "'v[0]', an element of 'v', is passed to 'touch' by mutable reference, and the same call hands it a view or data pointer"
# Their logs are read once both refusals have ended.
cases_end
for name in container_element_and_data_call container_element_and_span_call; do
    if grep -q "malformed" "$run/$name.log"; then
        echo "$name was refused as a malformed statement rather than at its source" >&2
        exit 1
    fi
    grep -q "error \[unsupported-semantics\]: verified function 'same_call'" "$run/$name.log"
done
# SPEC: VERIFIED-036
# Write access comes from the access path, never from the storage behind it,
# and a capability over zero elements says nothing of its pointer.
case_run refuse container_const_span_writable "'writable(s)' names elements declared const"
case_run refuse container_const_pointer_writable "'writable(p)' names elements declared const"
case_run refuse capability_non_null "verified function 'non_null' does not satisfy its contract"

# --- Storage generations: no view or reference outlives its storage (STDMODEL-015)

# SPEC: STDMODEL-015
case_run refuse container_stale_reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::push_back'"
case_run refuse container_stale_span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::push_back'"
case_run refuse container_stale_span_in_loop "'s' views the storage of 'v', which may have been reallocated or ended by the loop at"
case_run refuse container_reference_after_clear "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::clear'"
case_run refuse container_span_after_move "'s' views the storage of 'v', which may have been reallocated or ended by being moved from"
case_run refuse container_stale_after_mutable_call "which may have been reallocated or ended by passing it by mutable reference to 'grow'"
# SPEC: VERIFIED-039, VERIFIED-040
# A vector that may be the one a call may write is stale with it, for the reason
# that one is.
case_run refuse container_view_of_alias_after_pointer_call "'s' views the storage of 'c', which may have been reallocated or ended by passing it by mutable reference to 'peek' at"
case_run refuse container_string_stale_view "'view' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='"
# SPEC: STDMODEL-014
case_run refuse container_span_outlives_vector "span 's' is modeled only as a view of a whole vector or string this body tracks"
case_run refuse container_returned_span "it returns a span, and a view is not returned"
case_run refuse container_view_by_reference "span parameter 's' is modeled only by value"
# Two references may name one vector, and a reference may name its element.
case_run refuse container_external_alias "return path 'aliased path 1' does not satisfy its contract"
case_run refuse container_reference_alias "return path 'through_element path 1' does not satisfy its contract"

# --- Refined elements (STDMODEL-020) ----------------------------------------

# SPEC: STDMODEL-020
case_run refuse container_refined_push "this value is not shown to satisfy refinement type 'Positive'"
case_run refuse container_refined_write "this value is not shown to satisfy refinement type 'Positive'"
case_run refuse container_refined_span_write "this value is not shown to satisfy refinement type 'Positive'"
case_run refuse container_refined_value_initialized "this value is not shown to satisfy refinement type 'Positive'"
case_run refuse container_refined_parameter "parameter 'v' is a container of refined elements"
case_run refuse container_refined_writable_view "nothing obliges it to write values satisfying 'Positive'"
case_run refuse container_refined_copy "the elements of 'plain' are not known to satisfy 'Positive'"
case_run refuse container_refined_mutable_reference "'r' is passed to 'g' by mutable reference, and its elements must satisfy 'Positive'"
case_run refuse container_refined_result "its result is a container whose element type is written as the refinement 'Positive'"
case_run refuse container_refined_std_array "its element type is written as the refinement 'Positive', which std::array does not state"
case_run refuse container_refined_span_local "span 's' is declared with the refined element type 'Positive'"

# --- Moves and what is not modeled (STDMODEL-010, STDMODEL-019, STDMODEL-021)

# SPEC: STDMODEL-021
case_run refuse container_moved_from_size "return path 'moved_from path 1' does not satisfy its contract"
# SPEC: STDMODEL-019
case_run refuse container_unmodeled_member "'std::vector::at' is not a modeled operation of std::vector"
# SPEC: STDMODEL-010
case_run refuse container_vector_bool "std::vector<bool> is not modeled"
case_run refuse container_custom_allocator "a vector with an allocator other than std::allocator is not modeled"

cases_end
echo 'no verified body uses a container past its bounds, its storage or its model'
