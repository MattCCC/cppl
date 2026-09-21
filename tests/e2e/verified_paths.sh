#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/verified-paths.XXXXXX")
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/verified_paths.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Function contracts proven: +21$' "$run/report"
    grep -Eq '^Call preconditions proven: +9$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    "$run/program"
    grep -Fq 'if (x <= 10u)' "$run/runtime.cpp"
    grep -Fzq $'    if (identity(x) <= 10u)\n        return bounded(x);' "$run/runtime.cpp"
    if grep -Eq '\b(verified|ensures|expects|assert)\b' "$run/runtime.cpp"; then
        echo 'formal syntax or runtime checks survived erasure' >&2
        exit 1
    fi
    "$CLANG" "-std=$standard" -x c++-cpp-output -pedantic-errors -Werror \
        "$run/runtime.cpp" -o "$run/erased"
    "$run/erased"
done
echo 'all return paths and guarded calls verified without runtime changes'
