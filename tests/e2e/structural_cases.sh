#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/structural-cases.XXXXXX")
# 38. Every provider except std::expected is available in all three standards.
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/structural_cases.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Proof declarations proven: +44$' "$run/report"
    grep -Eq '^Laws proven: +1$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    test "$("$run/program")" = '7'
    # 32. No proof construct survives into the erased translation unit.
    ! grep -qE '(^|[^_[:alnum:]])(cases|decompose|valueless|components|non_null)[^_[:alnum:]]*(v|o|p|t|a|n|e|inner|maybe)? *[{(]' \
        "$run/runtime.cpp"
    ! grep -q 'alternative<0>\|=> *{\|proves (\|refl;' "$run/runtime.cpp"
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    test "$("$run/erased")" = "$("$run/program")"
done
# 5/38. std::expected is feature-gated on the C++23 library.
if "$CPPL" -std=c++23 -fsyntax-only "$FIXTURES/expected_cases.cpp" > /dev/null 2>&1; then
    "$CPPL" -std=c++23 "$FIXTURES/expected_cases.cpp" -o "$run/expected" \
        --cppl-trust-report "--cppl-emit-projection=$run/expected_runtime.cpp" > "$run/expected_report"
    grep -Eq '^Proof declarations proven: +11$' "$run/expected_report"
    grep -Eq '^Trusted external axioms: +0$' "$run/expected_report"
    test "$("$run/expected")" = '5'
    ! grep -q 'alternative<0>\|=> *{\|proves (\|refl;' "$run/expected_runtime.cpp"
    "$CLANG" -std=c++23 -x c++-cpp-output "$run/expected_runtime.cpp" -o "$run/expected_erased"
    test "$("$run/expected_erased")" = "$("$run/expected")"
    echo 'std::expected decomposes and erases'
else
    echo 'std::expected unavailable in this standard library; provider correctly not exercised'
fi
# 13. Adding an alternative must invalidate a proof that was exhaustive before.
sed 's/using Aliased = std::variant<int, bool>;/using Aliased = std::variant<int, bool, unsigned>;/' \
    "$FIXTURES/structural_cases.cpp" > "$run/added.cpp"
if "$CPPL" "$run/added.cpp" -o "$run/added" > "$run/added.log" 2>&1; then
    echo 'a newly added variant alternative was silently absorbed' >&2
    exit 1
fi
test ! -e "$run/added"
grep -q "non-exhaustive cases: 'alternative<2>' has no arm" "$run/added.log"
! grep -q PROVEN "$run/added.log"
# 13. The same holds for a field added to a product.
sed 's/^    int y;$/    int y;\n    bool z;/' "$FIXTURES/structural_cases.cpp" > "$run/field.cpp"
if "$CPPL" "$run/field.cpp" -o "$run/field" > "$run/field.log" 2>&1; then
    echo 'a newly added product field was silently absorbed' >&2
    exit 1
fi
test ! -e "$run/field"
grep -q 'product binds 3 value(s), but this arm names 2' "$run/field.log"
echo 'structural cases verify, survive erasure, and reject added alternatives and fields'
