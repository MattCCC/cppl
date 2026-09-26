#!/usr/bin/env bash
# What signed arithmetic, division and integer conversions cannot let through
# (SPEC.md 29, 31, Annex T; RFC 0019).
#
# Every operation C++ defines only under a condition owes that condition on the
# path that evaluates it. Each program here is one step past a boundary, or
# claims what C++ does not compute, and must be refused for that reason. Its
# accepted twin, one step inside, is in `fixtures/signed_arithmetic.cpp`,
# driven by `e2e/signed_arithmetic.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/signed-arithmetic.XXXXXX")

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

defined() {
    echo "an operation in verified function '$1' is not shown to have defined behavior: $2"
}

# SPEC: ARITH-006, ARITH-012, DEFINEDBEHAVIOR-001
# Representability at the boundary of every width. A narrow type promotes to
# `int`, which holds all its values and where the sum always fits, and the
# conversion back owes the bound. Each diagnostic names the operation, the
# condition, the types and the rule.
refuse arith_int32_max_plus_one \
    "arith_int32_max_plus_one.cpp:10:12: error [kernel-rejection]: $(defined int_at_max_plus_one \
        "signed overflow: 'x + 1' on the signed type 'int' is not shown to stay within 'int'")" \
    "note: C++ leaves a signed addition undefined unless its exact result is a value of its type" \
    "(SPEC.md ARITH-006, DEFINEDBEHAVIOR-001)"
refuse arith_int32_min_minus_one \
    "arith_int32_min_minus_one.cpp:10:12: error [kernel-rejection]: $(defined int_at_min_minus_one \
        "signed overflow: 'x - 1' on the signed type 'int'")"
refuse arith_int64_max_plus_one \
    "$(defined int64_at_max_plus_one "signed overflow: 'x + (long long)1' on the signed type 'long long'")"
refuse arith_int64_min_minus_one \
    "$(defined int64_at_min_minus_one "signed overflow: 'x - (long long)1' on the signed type 'long long'")"
refuse arith_int8_sum_past_max \
    "$(defined int8_sum_past_max \
        "unrepresentable conversion: '(int)a + (int)b' of type 'int' is not shown to be a value of 'signed char'")"
refuse arith_int16_sum_past_min \
    "$(defined int16_sum_past_min \
        "unrepresentable conversion: '(int)a + (int)b' of type 'int' is not shown to be a value of 'short'")"

# SPEC: ARITH-006, DEFINEDBEHAVIOR-001
# Products one factor past the bound, and the promoted product of two unsigned
# 16-bit values, which overflows `int`.
refuse arith_int32_large_product \
    "$(defined int32_scaled_too_far "signed overflow: 'x * 1000' on the signed type 'int'")" \
    "C++ leaves a signed multiplication undefined"
refuse arith_int64_large_product \
    "$(defined int64_doubled_too_far "signed overflow: 'x * (long long)2' on the signed type 'long long'")"
refuse arith_int16_unsigned_product \
    "$(defined unsigned_short_product "signed overflow: '(int)a * (int)b' on the signed type 'int'")"

# SPEC: ARITH-003, ARITH-006
# The operation's type is the common type after the integral promotions, read
# from Clang: two `unsigned char` operands add as `int`, so the 8-bit wrapped
# value is not the result. A type whose promotion is not modeled -- a character
# type that may promote to `unsigned int`, a bit-field that promotes by its
# width -- is refused where it is named, not given a promotion derived here.
refuse arith_unsigned_char_not_wrapped \
    "arith_unsigned_char_not_wrapped.cpp:11:12: error [kernel-rejection]: return path 'unsigned_char_wrapped path 1' does not satisfy its contract"
refuse arith_character_types_not_modeled \
    "'char32_sum' has a parameter of type 'char32_t', which is not modeled" \
    "'char16_sum' has a parameter of type 'char16_t', which is not modeled" \
    "'wide_sum' has a parameter of type 'wchar_t', which is not modeled"
refuse arith_bit_field_not_modeled \
    "arith_bit_field_not_modeled.cpp:12:14: error [unsupported-semantics]: verified function 'low_successor'" \
    "bit-field 'low' is not modeled: its values and its promotion follow its width, not its declared type" \
    "bit-field 'all' is not modeled"

# SPEC: ARITH-006, ARITH-009, DEFINEDBEHAVIOR-001
# An obligation is discharged only from what the path established before the
# evaluation: not from the result tested afterwards, not from the postcondition
# claimed of it, not from a modular identity the ring satisfies. Each contract
# here holds of the wrapped value; only the definedness is missing.
refuse arith_result_guard_too_late \
    "arith_result_guard_too_late.cpp:11:13: error [kernel-rejection]: $(defined successor_tested_after \
        "signed overflow: 'x + 1' on the signed type 'int'")"
refuse arith_postcondition_not_supposed \
    "arith_postcondition_not_supposed.cpp:9:12: error [kernel-rejection]: $(defined successor_claimed_above \
        "signed overflow: 'x + 1' on the signed type 'int'")"
refuse arith_modular_identity \
    "arith_modular_identity.cpp:11:12: error [kernel-rejection]: $(defined successor_by_identity \
        "signed overflow: 'x + 1' on the signed type 'int'")" \
    "arith_modular_identity.cpp:17:12: error [proof-failure]: verified function 'cancelled_by_identity' does not satisfy its contract"

# SPEC: ARITH-009, ARITH-003, DEFINEDBEHAVIOR-001
# The obligation belongs to each evaluation: a template's `a + b` owes it in the
# specialization whose common type is signed, and a callee's `x + 1` owes it
# once under the precondition every call site then owes.
refuse arith_template_signed_instance \
    "arith_template_signed_instance.cpp:11:12: error [kernel-rejection]: $(defined wrapped_sum \
        "signed overflow: 'a + b' on the signed type 'int'")"
refuse arith_call_site_precondition \
    "arith_call_site_precondition.cpp:21:12: error [kernel-rejection]: call-site precondition for 'second_successor -> successor' is not proven"

# SPEC: ARITH-006, EXPR-007
refuse arith_negate_min \
    "$(defined negated_anything "signed overflow: '-x' on the signed type 'int'")" \
    "C++ leaves a signed negation undefined"

# SPEC: ARITH-007, DEFINEDBEHAVIOR-002, DEFINEDBEHAVIOR-003
# A zero divisor, signed or unsigned, and the least value over -1 for both `/`
# and `%`.
refuse arith_division_by_zero \
    "arith_division_by_zero.cpp:7:12: error [kernel-rejection]: $(defined divided_by_nonnegative \
        "division by zero: the divisor 'y' of 'x / y' ('int') is not shown to be nonzero")" \
    "(SPEC.md ARITH-007, DEFINEDBEHAVIOR-002)"
refuse arith_remainder_by_zero \
    "$(defined bucket_of_any_size \
        "division by zero: the divisor 'size' of 'hash % size' ('unsigned int') is not shown to be nonzero")"
refuse arith_min_divided_by_minus_one \
    "$(defined divided_by_nonzero \
        "signed division overflow: 'x / y' on the signed type 'int' is not shown to avoid the least value of 'int' divided by -1")" \
    "(SPEC.md ARITH-007, DEFINEDBEHAVIOR-003)"
refuse arith_min_remainder_minus_one \
    "$(defined remainder_by_nonzero \
        "signed division overflow: 'x % y' on the signed type 'int' is not shown to avoid the least value")"

# SPEC: ARITH-007
# Truncation toward zero, and the remainder's sign, are C++'s: the floor and a
# nonnegative remainder are refused as the values.
refuse arith_floor_quotient \
    "arith_floor_quotient.cpp:8:12: error [kernel-rejection]: return path 'floored_quotient path 1' does not satisfy its contract"
refuse arith_positive_remainder \
    "arith_positive_remainder.cpp:8:12: error [kernel-rejection]: return path 'positive_remainder path 1' does not satisfy its contract"

# SPEC: ARITH-008
# A conversion to a signed type owes that the value fits, implicit or cast; one
# to an unsigned type reduces, and a comparison with `0u` converts the signed
# side.
refuse arith_narrowing_conversion \
    "$(defined narrowed_one_past \
        "unrepresentable conversion: 'x' of type 'int' is not shown to be a value of 'short', to which it is implicitly converted")" \
    "(SPEC.md ARITH-008)"
refuse arith_unsigned_to_int \
    "$(defined from_unsigned_one_past \
        "unrepresentable conversion: 'u' of type 'unsigned int' is not shown to be a value of 'int'")"
refuse arith_explicit_narrowing_cast \
    "$(defined cast_down_one_past \
        "unrepresentable conversion: 'x' of type 'int' is not shown to be a value of 'signed char', to which it is cast")"
refuse arith_mixed_signedness \
    "arith_mixed_signedness.cpp:11:12: error [proof-failure]: return path 'below_zero path 2' does not satisfy its contract"

# SPEC: ARITH-009, LOOP-001
# The obligation is owed on the path that evaluates the operation, under what
# that path knows: the loop's invariant, the arm of `?:`, the left of `||`.
refuse arith_loop_overflow \
    "arith_loop_overflow.cpp:13:9: error [kernel-rejection]: $(defined thousands_too_many \
        "signed overflow: 'total#2 + 1000' on the signed type 'int'")"
refuse arith_unguarded_arm \
    "arith_unguarded_arm.cpp:8:20: error [kernel-rejection]: $(defined away_from_zero \
        "signed overflow: 'x + 1' on the signed type 'int'")"
refuse arith_short_circuit_order \
    "arith_short_circuit_order.cpp:8:9: error [kernel-rejection]: $(defined ratio_tested_late \
        "division by zero: the divisor 'y' of 'x / y' ('int') is not shown to be nonzero")"
# SPEC: ARITH-009, BOUNDARYEX-001, DEFINEDBEHAVIOR-001
# Each arm of `?:` and the right of `&&` are owed where they run: where the
# condition holds or fails, and where the left holds. None of them excludes the
# boundary. The arms are in an initializer, one expression, so what protects
# each is the outcome its operation is owed under; `&&` is in a condition,
# whose right operand is a route of its own.
refuse arith_then_arm_overflow \
    "arith_then_arm_overflow.cpp:10:22: error [kernel-rejection]: $(defined successor_from_zero \
        "signed overflow: 'x + 1' on the signed type 'int'")"
refuse arith_else_arm_overflow \
    "arith_else_arm_overflow.cpp:10:26: error [kernel-rejection]: $(defined away_from_zero_below \
        "signed overflow: 'x - 1' on the signed type 'int'")"
refuse arith_right_of_and \
    "arith_right_of_and.cpp:8:18: error [kernel-rejection]: $(defined stepped_if_negative \
        "signed overflow: 'x - 1' on the signed type 'int'")"
refuse arith_refinement_bound \
    "$(defined scaled_small "signed overflow: '3000000 * x' on the signed type 'int'")"

# SPEC: ARITH-009, CORRECT-002
# A call C++ does not sequence before the operation may not have returned when
# it runs, so its postcondition is not supposed: a callee that never returns
# promises anything, and must not excuse the overflow.
refuse arith_unsequenced_partial_call \
    "arith_unsequenced_partial_call.cpp:20:24: error [kernel-rejection]: $(defined overflow_before_the_call \
        "signed overflow: 'x + 1' on the signed type 'int'")"

# SPEC: ARITH-010, ADMISSIBLE-005
# A specification is defined where it is stated, and a law is never claimed at
# a value that may not exist.
refuse arith_specification_overflow \
    "arith_specification_overflow.cpp:9:12: error [proof-failure]: verified function 'successor_is_larger' does not satisfy its contract" \
    "Eq<u1>(add_fits:i32(#0, 1:i32), 1:u1)"
refuse arith_law_instance_argument \
    "arith_law_instance_argument.cpp:9:23: error [unsupported-semantics]: proof 'reflexive_at_successor' claims law 'reflexive' at an argument whose behavior is not always defined: signed overflow: 'x + 1'"

# SPEC: ARITH-011
# A pure function is a total definition and owes nothing where it is called.
refuse arith_pure_signed_definition \
    "arith_pure_signed_definition.cpp:11:13: error [unsupported-semantics]: law 'twice_is_even' cannot be stated to the formal core: 'twice' is not available to the formal core as a definition" \
    "note: its body evaluates an operation C++ defines only under a condition (signed overflow: 'x + x' on the signed type 'int' is not shown to stay within 'int'), and a pure function is a total definition of the formal core, which owes none"

echo 'every operation past its boundary is refused, for the condition it owes'
