#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/verified-arithmetic.XXXXXX")
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/verified_arithmetic.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Laws proven: +5$' "$run/report"
    grep -Eq '^Function contracts proven: +15$' "$run/report"
    grep -Eq '^Call preconditions proven: +2$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Trusted solvers: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    grep -Eq '^Formal core version: +cppl-core-0\.8\.0$' "$run/report"
    "$run/program"
    grep -Fq 'return x * (y + z);' "$run/runtime.cpp"
    grep -Fq 'm = m - 1u;' "$run/runtime.cpp"
    grep -Fq 'return predecessor(predecessor(x));' "$run/runtime.cpp"
    if grep -Eq '\b(verified|ensures|expects|law|proof|assert)\b' "$run/runtime.cpp"; then
        echo 'formal syntax or runtime checks survived erasure' >&2
        exit 1
    fi
    "$CLANG" "-std=$standard" -x c++-cpp-output -pedantic-errors -Werror \
        "$run/runtime.cpp" -o "$run/erased"
    "$run/erased"
done
echo 'modular arithmetic and order consequences verified without runtime changes'
