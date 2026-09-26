#!/usr/bin/env bash
# Signed arithmetic, division and integer conversions, verified and run
# (SPEC.md 29, 31; RFC 0019).
#
# Every function in fixtures/signed_arithmetic.cpp proves the condition each of
# its operations owes, one step inside the boundary its twin in
# fixtures/negative/arith_*.cpp crosses. The program then runs and checks, at
# runtime, the values its contracts state; the erased program, compiled by
# Clang alone, runs the same. Verification adds no check: every operator and
# conversion reaches the runtime program exactly as written.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/signed-arithmetic.XXXXXX")

expect() {
    if ! grep -Eq "$1" "$run/report"; then
        echo "the report does not state: $1" >&2
        cat "$run/report" >&2
        exit 1
    fi
}

for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/signed_arithmetic.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    # SPEC: ARITH-006, ARITH-007, ARITH-008, ARITH-009
    expect '^Function contracts proven: +65$'
    expect '^  partial correctness only: +3$'
    expect '^Call preconditions proven: +2$'
    expect '^Defined operations proven: +82$'
    expect '^Loop invariants proven: +10$'
    expect '^Loop measures proven: +1$'
    expect '^Unresolved obligations: +0$'
    expect '^Trusted solvers: +0$'
    expect '^Trusted external axioms: +0$'
    expect '^Formal core version: +cppl-core-0\.8\.0$'
    "$run/program"

    # SPEC: RUNTIMECHECK-009, ERASE-003
    # The operations are the program's own, unchanged, and nothing checks them.
    for written in 'return x + 1;' 'return x - 1;' 'return a * b;' 'return x * 1000;' 'return -x;' \
        'return x / y;' 'return x % y;' 'return x / -2;' 'return hash % size;' \
        'return static_cast<signed char>(x);' 'total += 1000;' '--i;' 'y /= 2;' 'y %= 7;' \
        'return c - '"'"'0'"'"';' 'return bounded_half(x) * 2;'; do
        if ! grep -Fq -e "$written" "$run/runtime.cpp"; then
            echo "the runtime program lost: $written" >&2
            exit 1
        fi
    done
    if grep -Eq '\b(verified|ensures|expects|invariant|decreases|assert|abort|__builtin_trap)\b' "$run/runtime.cpp"; then
        echo 'formal syntax or a runtime check survived erasure' >&2
        exit 1
    fi
    "$CLANG" "-std=$standard" -x c++-cpp-output -pedantic-errors -Werror "$run/runtime.cpp" -o "$run/erased"
    "$run/erased"
done
echo 'signed arithmetic, division and conversions verified, with the runtime program unchanged'
