#!/usr/bin/env bash
# SPEC: EXPR-016, SPECEXPR-002, LOOP-004
# Conditions in verified bodies (C++ [expr.log.and], [expr.log.or]).
#
# `fixtures/conditions.cpp` states a contract for each shape: a loop invariant
# that is a disjunction of conjunctions, and one that is a conjunction holding
# a disjunction, each operand specified on its own; a ghost snapshot related
# to the loop case by case; callers taking apart the disjunction inside a
# callee's postcondition; a flag computed by a selection that decides a
# branch; and a disjunction about a selection no side of which holds alone.
# Every contract is proven in every supported standard, with nothing
# unresolved and no warning; the program prints what the contracts state; and
# erasure keeps each loop, condition and call as written.
# negative/conditions.sh refuses the false twins.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/conditions.XXXXXX")

expected='1 0 2 4
6 5 4 3 2
1 0 8 7'

fail() {
    echo "$1" >&2
    exit 1
}

for standard in c++17 c++20 c++23; do
    base="$run/$standard"
    if ! "$CPPL" "-std=$standard" "$FIXTURES/conditions.cpp" -o "$base.program" --cppl-trust-report \
        "--cppl-emit-projection=$base.runtime.ii" > "$base.report" 2> "$base.err"; then
        cat "$base.err" >&2
        fail "the conditions were refused ($standard)"
    fi
    if grep -q 'warning \[' "$base.err"; then
        cat "$base.err" >&2
        fail "verifying the conditions warned ($standard)"
    fi
    for line in 'Function contracts proven: +11' '  partial correctness only: +0' 'Loop invariants proven: +11' \
        'Loop measures proven: +5' 'Unresolved obligations: +0' 'Laws trusted: +0'; do
        grep -Eq "^$line\$" "$base.report" || { cat "$base.report" >&2; fail "the report does not state '$line'"; }
    done
    output=$("$base.program")
    [ "$output" = "$expected" ] || fail "the program printed '$output', not what its contracts state ($standard)"

    # Each loop, condition and call is the program; the contracts, loop clauses
    # and ghost declarations are not.
    for written in 'while (i > 0u)' 'if (i == mark) {' 'for (unsigned i = 0u; i < n; ++i)' 'if (i < limit) {' \
        'while (b != 0u)' 'return min_of(max_of(v, lo), hi);' 'const bool any = x > 0u ? true : y > 0u;' \
        'return count + (x > 0u ? 1u : 0u);'; do
        grep -Fq "$written" "$base.runtime.ii" || fail "the erased program lost: $written ($standard)"
    done
    if grep -Eq '(^|[^_[:alnum:]])(verified|ensures|expects|invariant|decreases|ghost)([^_[:alnum:]]|$)|__cppl' \
        "$base.runtime.ii"; then
        fail "formal syntax survived erasure ($standard)"
    fi
    "$CLANG" "-std=$standard" -x c++-cpp-output -pedantic-errors -Werror "$base.runtime.ii" -o "$base.erased"
    [ "$("$base.erased")" = "$expected" ] || fail "the erased program printed something else ($standard)"
done

# What was proven, contract by contract.
grep '^  contract of ' "$run/c++23.report" | sed 's/^  /proven: /; s/, identity [0-9a-f]*$//' | sort -u
echo 'every condition is verified, run as its contract states, and erased as written'
