#!/usr/bin/env bash
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/verified.XXXXXX")

for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/verified_functions.cpp" \
        -o "$run/program" --cppl-trust-report \
        "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Laws proven: +2$' "$run/report"
    grep -Eq '^Function contracts proven: +13$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    "$run/program"
    if grep -Eq '\b(verified|ensures|expects|__cppl_spec_)\b' "$run/runtime.cpp"; then
        echo 'contract syntax survived erasure' >&2
        exit 1
    fi
    "$CLANG" "-std=$standard" -x c++-cpp-output -pedantic-errors -Werror \
        "$run/runtime.cpp" -o "$run/erased"
    "$run/erased"
done

echo 'body-derived contracts are kernel checked, erased, and executed'
