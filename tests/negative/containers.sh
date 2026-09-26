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

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/containers.XXXXXX")

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
refuse container_array_out_of_bounds "law 'array_unguarded element index' is not proven"
# SPEC: STDMODEL-012
refuse container_vector_out_of_bounds "law 'vector_unguarded element index' is not proven"
refuse container_string_out_of_bounds "law 'string_unguarded element index' is not proven"
refuse container_span_out_of_bounds "law 'span_unbounded element index' is not proven"
# SPEC: STDMODEL-015
# A bound proved before `pop_back` bounds nothing after it.
refuse container_element_after_pop "law 'read_after_pop element index' is not proven"
# An element place formed before `pop_back` is not matched after it.
refuse container_element_reused_after_pop "law 'reread_after_pop element index' is not proven"
# SPEC: STDMODEL-011
refuse container_array_reference_parameter "an element of the std::array a reference designates is not modeled"

# --- Summaries owe their preconditions (STDMODEL-013, STDMODEL-023) ---------

# SPEC: STDMODEL-023
refuse container_pop_empty "call-site precondition for 'pop_unguarded -> std::vector::pop_back' is not proven"
# A by-value argument is a copy; the caller's vector is not what the callee grew.
refuse container_by_value_call "return path 'grown_by_callee path 1' does not satisfy its contract"

# --- A span holds no validity by existing (STDMODEL-016, STDMODEL-017) ------

# SPEC: STDMODEL-016
refuse container_span_without_capability "reading an element of 'in' requires 'readable(in)', which was not established"
refuse container_forward_without_capability "calling 'count' requires 'readable(in)', which is not established"
refuse container_span_after_unsafe "requires 'readable(in)', which no longer holds after the unsafe block"
# SPEC: STDMODEL-016
# Only a verified function's `expects` reads a capability conjoined with a
# predicate apart; anywhere else the capability would be lost unread.
refuse capability_conjoined_postcondition "the postcondition of verified function 'read_once' conjoins a memory capability with a predicate"
refuse capability_conjoined_trusted_law "law 'readable_and_one' conjoins a memory capability with a predicate"
refuse capability_conjoined_law_premise "the precondition of law 'positive_when_readable' conjoins a memory capability with a predicate"
refuse container_self_view_call "by a reference through which the callee may reallocate it"
# SPEC: STDMODEL-017
refuse container_data_overrun "call-site precondition for 'data_overrun -> clear_prefix' is not proven"

# --- Storage generations: no view or reference outlives its storage (STDMODEL-015)

# SPEC: STDMODEL-015
refuse container_stale_reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::push_back'"
refuse container_stale_span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::push_back'"
refuse container_stale_span_in_loop "'s' views the storage of 'v', which may have been reallocated or ended by the loop at"
refuse container_reference_after_clear "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::clear'"
refuse container_span_after_move "'s' views the storage of 'v', which may have been reallocated or ended by being moved from"
refuse container_stale_after_mutable_call "which may have been reallocated or ended by passing it by mutable reference to 'grow'"
refuse container_string_stale_view "'view' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='"
# SPEC: STDMODEL-014
refuse container_span_outlives_vector "span 's' is modeled only as a view of a whole vector or string this body tracks"
refuse container_returned_span "it returns a span, and a view is not returned"
refuse container_view_by_reference "span parameter 's' is modeled only by value"
# Two references may name one vector, and a reference may name its element.
refuse container_external_alias "return path 'aliased path 1' does not satisfy its contract"
refuse container_reference_alias "return path 'through_element path 1' does not satisfy its contract"

# --- Refined elements (STDMODEL-020) ----------------------------------------

# SPEC: STDMODEL-020
refuse container_refined_push "this value is not shown to satisfy refinement type 'Positive'"
refuse container_refined_write "this value is not shown to satisfy refinement type 'Positive'"
refuse container_refined_span_write "this value is not shown to satisfy refinement type 'Positive'"
refuse container_refined_value_initialized "this value is not shown to satisfy refinement type 'Positive'"
refuse container_refined_parameter "parameter 'v' is a container of refined elements"
refuse container_refined_writable_view "nothing obliges it to write values satisfying 'Positive'"
refuse container_refined_copy "the elements of 'plain' are not known to satisfy 'Positive'"

# --- Moves and what is not modeled (STDMODEL-010, STDMODEL-019, STDMODEL-021)

# SPEC: STDMODEL-021
refuse container_moved_from_size "return path 'moved_from path 1' does not satisfy its contract"
# SPEC: STDMODEL-019
refuse container_unmodeled_member "'std::vector::at' is not a modeled operation of std::vector"
# SPEC: STDMODEL-010
refuse container_vector_bool "std::vector<bool> is not modeled"
refuse container_custom_allocator "a vector with an allocator other than std::allocator is not modeled"

echo 'no verified body uses a container past its bounds, its storage or its model'
