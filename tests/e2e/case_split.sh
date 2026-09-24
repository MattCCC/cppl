#!/usr/bin/env bash
# Case splits on a runtime path (SPEC.md 20.7): verified in every standard mode,
# erased to empty statements, and the program the same as the one written
# without them.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/case-split.XXXXXX")

for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/case_split.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Proof declarations proven: +1$' "$run/report"
    grep -Eq '^Function contracts proven: +14$' "$run/report"
    # A split walked once per path reaching it owes its omissions on each path.
    grep -Eq '^Omitted cases proven: +27$' "$run/report"
    grep -Eq '^Impossible paths proven: +1$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    test "$("$run/program")" = '1 1 1 1 0 5 1 3 1 1 1 1'

    # SPEC: ERASE-016
    # Nothing of any split survives in the program the fixture's own text
    # becomes, and the one written as the body of an unbraced `if` leaves a `;`.
    start=$(grep -n '^enum class Mode' "$run/runtime.cpp" | head -1 | cut -d: -f1)
    tail -n "+$start" "$run/runtime.cpp" > "$run/own.cpp"
    ! grep -Eq '(^|[^_[:alnum:]])(cases|decompose|omit|components|unnamed|non_null|contradiction)([^_[:alnum:]]|$)' \
        "$run/own.cpp"
    ! grep -q '[^<]=>' "$run/own.cpp"
    awk '/if \(flag\)/ { armed = 1; next } armed && /[^[:space:]]/ { print; exit }' "$run/own.cpp" \
        | grep -Eq '^[[:space:]]*;$'
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    test "$("$run/erased")" = "$("$run/program")"
done

# SPEC: WORD-012
# Where `cases` names a type, the statement is a declaration with a braced
# initializer, as C++ reads it, and the implementation says it is not a split.
"$CPPL" -std=c++20 "$FIXTURES/case_split_as_cpp_name.cpp" -o "$run/named" > "$run/named.log" 2>&1
grep -q "'cases' is also a name in this translation unit, so this statement is ordinary C++, not a case split" \
    "$run/named.log"
test "$("$run/named")" = '4 5'

echo 'case splits on runtime paths verify, erase to empty statements, and leave C++ uses of the word alone'
