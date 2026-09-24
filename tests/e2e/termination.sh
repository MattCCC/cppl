#!/usr/bin/env bash
# SPEC: TERMINATION-004, TERMINATION-005, TERMINATION-006, TERMINATION-007
# SPEC: CORRECT-003, CORRECT-004, CORRECT-006, LOOP-001, LOOP-003, LOOP-006
# Termination: measures on loops and functions (SPEC.md 22, 23, 24.3).
#
# Every descent is an obligation of its own, reported apart from invariants:
# one per continuing path of a loop, one per call within a recursion group. A
# contract whose every loop and callee terminates is total; the report counts
# and names the ones that are not. Measures erase with the rest of the
# contract, and no counter or check reaches the program.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/termination.XXXXXX")

binary="$run/termination"
report="$run/termination.report"
runtime="$run/runtime.ii"

"$CPPL" -std=c++20 "$FIXTURES/termination.cpp" -o "$binary" --cppl-trust-report \
    "--cppl-emit-projection=$runtime" > "$report"

expect() {
    if ! grep -Eq "$1" "$report"; then
        echo "the trust report does not state: $1" >&2
        cat "$report" >&2
        exit 1
    fi
}

expect '^Function contracts proven: +13$'
expect '^  partial correctness only: +1$'
expect '^Call preconditions proven: +1$'
expect '^Loop invariants proven: +20$'
expect '^Loop measures proven: +9$'
expect '^Recursive call measures proven: +7$'
expect '^Unresolved obligations: +0$'
expect '^Laws trusted: +0$'
# The one partial contract is named, so a count is never all that says which.
expect '^Partial-correctness contracts: +1$'
expect '^  contract of partial \(.*termination\.cpp:[0-9]+\), identity [0-9a-f]{16}$'

output=$("$binary")
if [ "$output" != "0 0 7 0 0 0 0 9 4 3 6 2" ]; then
    echo "expected the program to print its results, got '$output'" >&2
    exit 1
fi

# The fixture's own text in the program, after the headers it includes: no
# measure, invariant or other clause survives, loops and recursion are as
# written, and nothing counts iterations or checks a measure at runtime.
program=$(sed -n '/termination\.cpp" 2$/,$p' "$runtime")
if [ -z "$program" ]; then
    echo 'the fixture was not found in its runtime projection' >&2
    exit 1
fi
if grep -Eq '(^|[^_[:alnum:]])(decreases|invariant|ensures|expects|verified|ghost)([^_[:alnum:]]|$)|__cppl_' \
    <<< "$program"; then
    echo 'a termination clause or its scaffolding survived into the erased translation unit' >&2
    exit 1
fi
for written in 'return count_down(n - 1u);' 'return ackermann(m - 1u, ackermann(m, n - 1u));' \
    'for (;;)' '} while (i > 0u);' 'continue;' 'break;'; do
    if ! grep -Fq "$written" <<< "$program"; then
        echo "the erased program lost: $written" >&2
        exit 1
    fi
done

# The report is the same on every run.
"$CPPL" -std=c++20 "$FIXTURES/termination.cpp" -o "$run/again" --cppl-trust-report > "$run/again.report"
if ! diff -q "$report" "$run/again.report" > /dev/null; then
    echo 'the trust report is not deterministic' >&2
    diff -u "$report" "$run/again.report" >&2
    exit 1
fi

echo 'every descent is proven where it is owed, and termination leaves nothing in the program'
