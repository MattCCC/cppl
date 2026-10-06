#!/usr/bin/env bash
# Range-based for loops in verified bodies.
#
# SPEC: LOOP-001, LOOP-004, STMT-005, STDMODEL-019, CONSTRUCT-094, TERMINATION-004
#
# A range-based for over a vector, a string or a span the body names, or over
# an array, is verified as C++ iterates it: one element per position, from the
# first to the last, each read where it owes its bound, the loop variable
# initialized from it or bound to it, the invariant at each head before the
# loop variable, and the positions left as a measure the kernel checks, so
# every such loop is total without a written `decreases`. In c++20 and c++23:
#
#   - fixtures/equivalence/range_for.cpp verifies -- by value, by reference and
#     by const reference, over vectors, a string, a span, an array local, a
#     std::array local and an array a parameter designates, with writes through
#     references owing a refined element type, `break`, `continue`, an early
#     return, nested loops and one inside a while loop -- and its program
#     prints what its contracts state, empty ranges included;
#   - the program keeps every range-based for as written and nothing generated
#     for the analysis (tests/e2e/erasure_equivalence.sh compares it with its
#     erasure by hand, code and text).
#
# Every refusal is in tests/negative/range_for.sh.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/range-for.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

expected='sum_vector(v) == 17, sum_vector(empty) == 0
count_spaces(text) == 1
sum_span(v) == 17, sum_span(empty) == 0
sum_arrays() == 15
largest_digit() == 9
reset_digits() == 2
upcase_a("a banana") == A bAnAnA
first_over(v, 5) == 12, first_over(v, 20) == 0
sum_to_zero(v) == 13
pairs(v) == 323
largest_capped(values) == 7'

for standard in c++20 c++23; do
    base="$run/fixture-$standard"
    "$CPPL" "-std=$standard" "$FIXTURES/equivalence/range_for.cpp" -o "$base" --cppl-trust-report \
        "--cppl-emit-projection=$base.runtime.ii" > "$base.report"
    grep -Eq '^Function contracts proven: +11$' "$base.report" || fail "not every contract was proven ($standard)"
    grep -Eq '^  partial correctness only: +0$' "$base.report" || fail "a range-based for was not shown to terminate ($standard)"
    grep -Eq '^Loop invariants proven: +9$' "$base.report" || fail "not every loop invariant was proven ($standard)"
    grep -Eq '^Loop measures proven: +20$' "$base.report" || fail "not every loop measure was proven ($standard)"
    grep -Eq '^Unresolved obligations: +0$' "$base.report" || fail "an obligation is unresolved ($standard)"
    for function in sum_vector count_spaces sum_span sum_arrays largest_digit reset_digits upcase_a first_over \
        sum_to_zero pairs largest_capped; do
        grep -Eq "^  contract of $function \\(" "$base.report" || fail "the contract of $function is not listed ($standard)"
    done
    [ "$("$base")" = "$expected" ] || fail "the program printed other values than its contracts state ($standard)"

    # The program keeps each range-based for as written, and no clause.
    for written in 'for (unsigned x : v) {' 'for (char c : text) {' 'for (unsigned x : s) {' \
        'for (const unsigned& x : a) {' 'for (const auto& y : b) {' 'for (Digit d : digits)' \
        'for (Digit& d : digits) {' 'for (char& c : text) {' 'for (unsigned long value : values)'; do
        grep -Fq "$written" "$base.runtime.ii" || fail "the program lost '$written' ($standard)"
    done
    if grep -q '__cppl_' "$base.runtime.ii"; then
        fail "analysis scaffolding reached the program ($standard)"
    fi
    for clause in 'invariant (best < 10u)' 'invariant (rounds <= 2u)' 'decreases (2u - rounds)' \
        'invariant (best <= 9ul)'; do
        if grep -Fq "$clause" "$base.runtime.ii"; then
            fail "the loop clause '$clause' reached the program ($standard)"
        fi
    done
    "$CLANG" "-std=$standard" -x c++-cpp-output "$base.runtime.ii" -o "$base.erased"
    [ "$("$base.erased")" = "$expected" ] || fail "the erased program behaves differently ($standard)"
done

echo "a range-based for is verified as C++ iterates it, terminates by the positions left, and stays as written"
