#!/usr/bin/env bash
# SPEC: STDMODEL-012, STDMODEL-015, STDMODEL-016, STDMODEL-019, STDMODEL-021, STDMODEL-023, STDMODEL-025
# SPEC: ARITH-003, VERIFIED-039, VERIFIED-040, UNSAFE-003, TUBOUND-004
# TRUST.md TCB-LIB-006, TCB-LIB-007
#
# Adversarial programs against the verified sequence subset (RFC 0020): storage
# used after an operation that may replace it, bounds that are off by one at 0,
# `size()` and SIZE_MAX, two views of one storage, a moved-from vector, what the
# model leaves out, and the same attacks through a contract imported from another
# translation unit. Each is refused for its stated reason, produces no program,
# and is described as proven nowhere. The accepted twin of each, differing in
# the one step that makes it unsound, is in `fixtures/sequence_attacks.cpp` or
# `fixtures/sequence_attacks_cross_tu/`, driven by `e2e/sequence_attacks.sh`.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
NEGATIVE="$FIXTURES/negative"
UNITS="$FIXTURES/sequence_attacks_cross_tu"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/sequence-attacks.XXXXXX")
fail() {
    echo "$1" >&2
    exit 1
}
# refuse <name> <diagnostic> [cppl arguments...]
refuse() {
    local name="$1" diagnostic="$2"
    shift 2
    local status=0
    "$CPPL" -std=c++20 -I "$UNITS" --cppl-trust-report "$NEGATIVE/$name.cpp" -o "$run/$name" "$@" \
        > "$run/$name.log" 2>&1 || status=$?
    [ "$status" -ne 0 ] || { cat "$run/$name.log" >&2; fail "$name was accepted"; }
    [ ! -e "$run/$name" ] || fail "a program was produced for $name"
    if grep -qE "PROVEN|C\+\+L Trust Report" "$run/$name.log"; then
        cat "$run/$name.log" >&2
        fail "$name was described as proven"
    fi
    if ! grep -qF -- "$diagnostic" "$run/$name.log"; then
        cat "$run/$name.log" >&2
        fail "$name did not fail for the stated reason"
    fi
}
stale="a view or element reference is used only while the storage it was formed over is unchanged"

# --- Storage generations (STDMODEL-015, STDMODEL-025) -----------------------
refuse sequence_attack_stale_after_reserve "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::reserve'"
refuse sequence_attack_reference_after_assign "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::operator='"
refuse sequence_attack_reference_after_move_assign "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::operator='"
refuse sequence_attack_string_reference_after_append \
    "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='"
refuse sequence_attack_reference_across_unsafe "'r' refers to an element of 'v', which may have been reallocated or ended by the unsafe block"
refuse sequence_attack_view_across_unsafe "return path 'through_view path 1' does not satisfy its contract"
for name in sequence_attack_stale_after_reserve sequence_attack_reference_after_assign \
    sequence_attack_reference_after_move_assign sequence_attack_string_reference_after_append \
    sequence_attack_reference_across_unsafe; do
    grep -qF "$stale" "$run/$name.log" || fail "$name does not state the storage rule"
done

# --- Bounds at 0, size() and SIZE_MAX (STDMODEL-012, ARITH-003) -------------
refuse sequence_attack_off_by_one "law 'off_by_one element index' is not proven"
refuse sequence_attack_last_of_empty "law 'last_of element index' is not proven"
refuse sequence_attack_first_of_empty "law 'first_of_empty element index' is not proven"
refuse sequence_attack_wrapping_guard "law 'wrapping_guard element index' is not proven"

# --- Two views of one storage (STDMODEL-016, VERIFIED-039, VERIFIED-040) ----
refuse sequence_attack_two_views "return path 'through_two path 1' does not satisfy its contract"

# --- Moved from (STDMODEL-021) ----------------------------------------------
refuse sequence_attack_moved_from_element "law 'moved_from_element element index' is not proven"

# --- What the model leaves out is refused, not approximated (STDMODEL-019) --
refuse sequence_attack_resize "'std::vector::resize' is not a modeled operation of std::vector (SPEC.md STDMODEL-019)"
refuse sequence_attack_iterator "local 'it' has type"
refuse sequence_attack_range_for "range-based for loops are not modeled"

# --- The same attacks across translation units (TUBOUND-004, STDMODEL-023) --
"$CPPL" -std=c++20 -I "$UNITS" -c "$UNITS/storage.cpp" -o "$run/storage.o" \
    "--cppl-emit-interface=$run/storage.cppli" > "$run/storage.log" 2>&1 ||
    { cat "$run/storage.log" >&2; fail "the producing unit is refused"; }
imported=("-c" "--cppl-import-interface=$run/storage.cppli")
refuse sequence_attack_xtu_stale "'s' views the storage of 'v', which may have been reallocated or ended by passing it by mutable reference to 'append_one'" \
    "${imported[@]}"
refuse sequence_attack_xtu_past_end "law 'past_the_end element index' is not proven" "${imported[@]}"
refuse sequence_attack_xtu_unguarded_last "call-site precondition for 'unguarded_last -> last_index' is not proven" "${imported[@]}"

echo 'every storage, bounds and aliasing attack on the sequence subset is refused'
