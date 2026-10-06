#!/usr/bin/env bash
# SPEC: STMT-002, CONSTRUCT-089, CONSTRUCT-090
# `if` statements with an init-statement, a condition variable and `if
# constexpr` in verified bodies (C++ [stmt.if]).
#
# `fixtures/if_statements.cpp` states an exact contract for each: an
# init-statement that declares, that writes, and that calls a function with an
# effect, each visible in the condition and in both branches; an
# init-statement followed by a call that decides; a condition variable; `if
# constexpr` chains in a template, where only the selected branch is
# instantiated and the others would not return; and `if constexpr` with an
# init-statement. Every contract is proven in every supported standard, the
# program prints what the contracts state, and erasure keeps each `if` as
# written. negative/if_statements.sh refuses the false twins.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/if-statements.XXXXXX")

expected='10 4 7 5
1 14 50 8
5 6 9 7 7 0'

fail() {
    echo "$1" >&2
    exit 1
}

for standard in c++17 c++20 c++23; do
    base="$run/$standard"
    if ! "$CPPL" "-std=$standard" "$FIXTURES/if_statements.cpp" -o "$base.program" --cppl-trust-report \
        "--cppl-emit-projection=$base.runtime.ii" > "$base.report" 2> "$base.err"; then
        cat "$base.err" >&2
        fail "the if statements were refused ($standard)"
    fi
    [ ! -s "$base.err" ] || { cat "$base.err" >&2; fail "verifying the if statements warned ($standard)"; }
    for line in 'Function contracts proven: +11' '  partial correctness only: +0' 'Unresolved obligations: +0'; do
        grep -Eq "^$line\$" "$base.report" || { cat "$base.report" >&2; fail "the report does not state '$line'"; }
    done
    output=$("$base.program")
    [ "$output" = "$expected" ] || fail "the program printed '$output', not what its contracts state ($standard)"
    for written in 'if (unsigned v = x + 1u; v > 5u) {' 'if (y = 7u; x == 0u) {' \
        'if (unsigned was = bump(c, c); was == 0u) {' 'if (unsigned copy = x) {' 'if constexpr (N > 1u) {' \
        'if (y = 7u; above(x)) {' 'if constexpr (constexpr unsigned two = 2u; two > 1u) {'; do
        grep -Fq "$written" "$base.runtime.ii" || fail "the erased program lost: $written ($standard)"
    done
    if grep -Eq '(^|[^_[:alnum:]])(verified|ensures|expects)([^_[:alnum:]]|$)|__cppl' "$base.runtime.ii"; then
        fail "formal syntax survived erasure ($standard)"
    fi
    "$CLANG" "-std=$standard" -x c++-cpp-output -pedantic-errors -Werror "$base.runtime.ii" -o "$base.erased"
    [ "$("$base.erased")" = "$expected" ] || fail "the erased program printed something else ($standard)"
done

# What was proven, contract by contract.
grep '^  contract of ' "$run/c++23.report" | sed 's/^  /proven: /; s/, identity [0-9a-f]*$//' | sort -u
echo 'every if form is verified, run as its contract states, and erased as written'
