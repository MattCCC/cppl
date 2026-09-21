#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/verified-locals.XXXXXX")
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/verified_locals.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Function contracts proven: +24$' "$run/report"
    grep -Eq '^Call preconditions proven: +1$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    "$run/program"
    grep -Fq 'unsigned y = x + 1u;' "$run/runtime.cpp"
    grep -Fq 'y = 7u;' "$run/runtime.cpp"
    grep -Fq 'unsigned y{x};' "$run/runtime.cpp"
    grep -Fzq $'    if (b)\n        x = 2u;' "$run/runtime.cpp"
    grep -Fq 'y = identity(x);' "$run/runtime.cpp"
    grep -Fq 'y *= 3ul;' "$run/runtime.cpp"
    grep -Fq '    y++;' "$run/runtime.cpp"
    if grep -Eq '\b(verified|ensures|expects|assert)\b' "$run/runtime.cpp"; then
        echo 'formal syntax or runtime checks survived erasure' >&2
        exit 1
    fi
    "$CLANG" "-std=$standard" -x c++-cpp-output -pedantic-errors -Werror \
        "$run/runtime.cpp" -o "$run/erased"
    "$run/erased"
done
echo 'locals, assignments and their calls verified without runtime changes'
