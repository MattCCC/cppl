#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/verified-loops.XXXXXX")
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/verified_loops.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Function contracts proven: +14$' "$run/report"
    grep -Eq '^  partial correctness only: +12$' "$run/report"
    grep -Eq '^Call preconditions proven: +3$' "$run/report"
    grep -Eq '^Loop invariants proven: +32$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    "$run/program"
    # Loops, their headers and bodies are the program; invariants are not.
    grep -Fq 'while (i < n)' "$run/runtime.cpp"
    grep -Fq 'for (unsigned i = 0u; i < n; ++i)' "$run/runtime.cpp"
    grep -Fq 'i = bounded_step(i);' "$run/runtime.cpp"
    grep -Fq 'break;' "$run/runtime.cpp"
    grep -Fq 'continue;' "$run/runtime.cpp"
    if grep -Eq '\b(verified|ensures|expects|invariant|decreases|assert)\b|__cppl' "$run/runtime.cpp"; then
        echo 'formal syntax or runtime checks survived erasure' >&2
        exit 1
    fi
    "$CLANG" "-std=$standard" -x c++-cpp-output -pedantic-errors -Werror \
        "$run/runtime.cpp" -o "$run/erased"
    "$run/erased"
done
echo 'loops verified against their invariants without runtime changes'
