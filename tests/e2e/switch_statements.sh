#!/usr/bin/env bash
# SPEC: STMT-003, CONSTRUCT-091, CONSTRUCT-052, CONSTRUCT-085, CONSTRUCT-096, CONSTRUCT-097
# `switch` statements and statement-level commas in verified bodies (C++
# [stmt.switch], [expr.comma]).
#
# `fixtures/switch_statements.cpp` states an exact contract for each form:
# fall-through with and without `[[fallthrough]]`, `break`, `default:` first, in
# the middle and absent, a scoped enumeration and unscoped enumerators, a switch
# in a loop with `break` and `continue`, a loop in a switch with `break`, nested
# switches, a condition whose call has an effect and is evaluated once, a
# condition variable, returns in cases, a case claimed not to occur, an unsafe
# block in a case, and commas as statements and in a `for` increment. Every
# contract is proven in every supported standard, totally but for the one that
# passes through the unsafe block; the program prints what the contracts state;
# and erasure keeps each switch and comma as written.
# negative/switch_statements.sh refuses the false twins.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/switch-statements.XXXXXX")

expected='1 1 2
11 10 100 0 2
7 1 3 5 40
8 3 20 6 5
12 10 11 22 36 105
21 5 0'

fail() {
    echo "$1" >&2
    exit 1
}

for standard in c++17 c++20 c++23; do
    base="$run/$standard"
    if ! "$CPPL" "-std=$standard" "$FIXTURES/switch_statements.cpp" -o "$base.program" --cppl-trust-report \
        "--cppl-emit-projection=$base.runtime.ii" > "$base.report" 2> "$base.err"; then
        cat "$base.err" >&2
        fail "the switch statements were refused ($standard)"
    fi
    # The one warning: the contract resting on the unsafe block in a case is
    # proven for partial correctness only (SPEC.md CORRECT-003).
    partial="warning \[partial-correctness\]: the contract of 'guarded' is proven for partial correctness only: \
it passes through the unsafe block at"
    if [ "$(grep -c 'warning \[' "$base.err")" != 1 ] || ! grep -q "$partial" "$base.err"; then
        cat "$base.err" >&2
        fail "verifying the switch statements warned of something else ($standard)"
    fi
    for line in 'Function contracts proven: +15' '  partial correctness only: +1' 'Call preconditions proven: +1' \
        'Impossible paths proven: +1' 'Unresolved obligations: +0' 'Laws trusted: +0'; do
        grep -Eq "^$line\$" "$base.report" || { cat "$base.report" >&2; fail "the report does not state '$line'"; }
    done
    output=$("$base.program")
    [ "$output" = "$expected" ] || fail "the program printed '$output', not what its contracts state ($standard)"

    # Each switch, label, `[[fallthrough]]` and comma is the program; the
    # contracts and loop clauses are not.
    for written in 'switch (x) {' 'case 1u:' 'default:' '[[fallthrough]];' 'case Color::red:' 'case one:' \
        'switch (bump(c, c)) {' 'switch (unsigned v = x + 1u) {' 'a = 1u, b = 2u, c = 3u;' 'y = sample();' \
        'for (unsigned i = 0u, j = n; i < n; ++i, --j)' 's = s + 1u, s = s + 2u;' 'continue;' 'break;'; do
        grep -Fq "$written" "$base.runtime.ii" || fail "the erased program lost: $written ($standard)"
    done
    if grep -Eq '(^|[^_[:alnum:]])(verified|ensures|expects|invariant|decreases)([^_[:alnum:]]|$)|__cppl' \
        "$base.runtime.ii"; then
        fail "formal syntax survived erasure ($standard)"
    fi
    "$CLANG" "-std=$standard" -x c++-cpp-output -pedantic-errors -Werror "$base.runtime.ii" -o "$base.erased"
    [ "$("$base.erased")" = "$expected" ] || fail "the erased program printed something else ($standard)"
done

# What was proven, contract by contract.
grep '^  contract of ' "$run/c++23.report" | sed 's/^  /proven: /; s/, identity [0-9a-f]*$//' | sort -u
echo 'every switch and comma form is verified, run as its contract states, and erased as written'
