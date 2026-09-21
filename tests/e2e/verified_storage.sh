#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/verified-storage.XXXXXX")
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/verified_storage.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Function contracts proven: +17$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    test "$("$run/program")" = '1 1 2'
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    test "$("$run/erased")" = "$("$run/program")"
    grep -q 'void set(int& x)' "$run/runtime.cpp"
    grep -q 'set(x)' "$run/runtime.cpp"
done
