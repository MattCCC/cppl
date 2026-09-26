#!/usr/bin/env bash
# SPEC: ERASE-002, ERASE-003, ERASE-004, ERASE-005, ERASE-006, ERASE-007, ERASE-010, ERASE-016
# SPEC: ERASEMATRIX-001, ERASEMATRIX-002, WORD-008
# TRUST.md TCB-ERASE-001, TCB-ERASE-002, TCB-ERASE-003, TCB-ERASE-004, TCB-ERASE-010
#
# Erased C++L behaves as, and is the same code as, the same program written in
# ordinary C++.
#
# Each C++L fixture in `fixtures/equivalence/` has a `.reference.cpp` twin: the
# same program erased by hand, as SPEC.md Annex M says each construct erases.
# The twin does not come from this implementation, so it can show erasure wrong
# where comparing the compiler's output with itself could not.
#
# In every supported standard, the C++L program and its twin must behave the
# same, printing the same output and exiting the same way, and must be the same
# code: identical assembly at -O0, where nothing is optimized away, and at -O2,
# where what is left is what runs. Identical code is what rules out a hidden
# check that happens to pass on the test input, a proof-only branch, a tag or a
# field, and a changed signature, on paths no input takes as well.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/equivalence"
WORK="$3"
CLANG="$4"
# shellcheck source=../support/equivalence.sh
source "$(dirname "$0")/../support/equivalence.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/erasure-equivalence.XXXXXX")

# The comparison is not vacuous. Programs that differ from an erased one only by
# a hidden runtime check, or only by a hidden field, are told apart from it at
# both levels.
for level in -O0 -O2; do
    for variant in erased checked tagged; do
        assembly "$run/tampered-$variant$level" "$CLANG" -std=c++17 "$level" "$FIXTURES/tampered/$variant.cpp"
    done
    for variant in checked tagged; do
        if cmp -s "$run/tampered-erased$level" "$run/tampered-$variant$level"; then
            echo "the comparison cannot see a $variant program at $level" >&2
            exit 1
        fi
    done
done

# equivalent <fixture> <expected output> <trust-report line>...
#
# The report lines pin what the fixture exercises, so a construct that stopped
# being recognized, and so stopped being erased at all, cannot pass unnoticed.
equivalent() {
    local fixture="$1" expected="$2"
    shift 2
    local standard level line output
    for standard in c++17 c++20 c++23; do
        local base="$run/$fixture-$standard"
        "$CPPL" "-std=$standard" "$FIXTURES/$fixture.cpp" -o "$base.cppl" --cppl-trust-report \
            "--cppl-emit-projection=$base.runtime.ii" > "$base.report"
        for line in 'Unresolved obligations: +0' "$@"; do
            if ! grep -Eq "^$line\$" "$base.report"; then
                echo "$fixture ($standard) does not report '$line'" >&2
                cat "$base.report" >&2
                exit 1
            fi
        done
        # Nothing generated for the analysis reaches the program.
        if grep -q '__cppl_' "$base.runtime.ii"; then
            echo "analysis scaffolding reached the runtime program of $fixture ($standard)" >&2
            exit 1
        fi

        "$CLANG" "-std=$standard" "$FIXTURES/$fixture.reference.cpp" -o "$base.reference"
        output=$("$base.cppl")
        if [ "$output" != "$expected" ]; then
            printf '%s (%s) printed\n%s\nexpected\n%s\n' "$fixture" "$standard" "$output" "$expected" >&2
            exit 1
        fi
        if [ "$("$base.reference")" != "$output" ]; then
            echo "$fixture ($standard) and its reference behave differently" >&2
            exit 1
        fi

        for level in -O0 -O2; do
            assembly "$base$level.cppl" "$CPPL" "-std=$standard" "$level" "$FIXTURES/$fixture.cpp"
            assembly "$base$level.reference" "$CLANG" "-std=$standard" "$level" "$FIXTURES/$fixture.reference.cpp"
            same_code "$fixture ($standard, $level)" "$base$level.cppl" "$base$level.reference"
        done
    done
}

# Laws, a trusted law, proofs with refl, exact, apply, assume and rewrite, cases
# with omitted cases, decompose and contradiction erase to nothing, and `pure`
# leaves its function as written.
equivalent proofs '0 7 7 3 3 4' \
    'Laws proven: +7' 'Laws trusted: +1' 'Omitted cases proven: +2'

# Contracts, loop invariants and measures leave the functions and loops as
# written, the other specifiers stay, and each claim that a path cannot occur
# leaves an empty statement.
equivalent contracts '4 8 5 6 7 10 10 5 6 0 3 2 4 1 1' \
    'Function contracts proven: +15' 'Loop invariants proven: +8' 'Loop measures proven: +1' \
    'Impossible paths proven: +3'

# Refinements lower to their base type's alias: the same layout, the same type
# identity, the same construction and destruction.
equivalent refinements $'2 50 9 10 70 3 9\n16 4 4 8 12\n1 1 1 1\n2 1' \
    'Function contracts proven: +9' 'Loop invariants proven: +2'

# Every contextual word used as an ordinary name survives erasure of the same
# words used as C++L in the same unit.
equivalent contextual_words '136 36 55 78 17 5 7 6 7' \
    'Laws proven: +1' 'Function contracts proven: +3'

# SPEC: TERMINATION-004, LOOP-001
# Measures leave with the other clauses, on functions and on every loop form,
# and recursion and loops stay exactly as written: no counter, no check.
equivalent termination '0 9 0 6 0' \
    'Function contracts proven: +6' '  partial correctness only: +0' 'Recursive call measures proven: +5'

# SPEC: ERASE-011, GHOST-001
# A ghost declaration leaves whole, at the top of a body and inside a loop, one
# declaring two ghosts and one calling a pure function alike.
equivalent ghost_state '3 4' \
    'Function contracts proven: +2' 'Loop invariants proven: +4'

# SPEC: UNSAFE-001
# `unsafe` leaves a function declaration and a block alike: the function, the
# block's braces and every statement in it stay, and run as written.
equivalent unsafe_boundaries '42 3 9 4' \
    'Function contracts proven: +2' '  relying on unsafe code: +2' 'Unsafe regions: +5'

# SPEC: CLASS-013, ABI-001
# A verified member function erases to the member function as written: const,
# mutating, static, overloaded, reference-qualified and declared in its class,
# with its class's members, layout and calling convention unchanged.
equivalent methods $'6 6 7 8 5 2 9\n6 0' \
    'Function contracts proven: +11' 'Call preconditions proven: +2' 'Loop invariants proven: +2'

echo 'erased C++L behaves as, and is the same code as, its ordinary C++ erasure in c++17, c++20 and c++23'
