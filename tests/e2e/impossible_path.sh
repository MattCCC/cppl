#!/usr/bin/env bash
# SPEC: VERIFIED-023, VERIFIED-045, CASE-012, CASE-016, WORD-011, ERASE-016
# TRUST.md TCB-CFG-006, TCB-ERASE-010
# `contradiction evidence;` in a verified body claims a runtime path cannot
# occur. Each claim is an obligation of its own, discharged by checked
# contradiction evidence, and counted apart from omitted cases. The statement
# erases to an empty statement. The rejections live in `fixtures/negative/`,
# driven by `negative/impossible_paths.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/impossible-path.XXXXXX")

"$CPPL" "$FIXTURES/impossible_path.cpp" -o "$run/program" --cppl-trust-report \
    "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"

# Six claims, each proven, and each counted as an impossible path rather than
# as an omitted case. Every function with a claim has partial-correctness
# conditions: a path that ends in a claim returns no value.
grep -Eq "^Impossible paths proven: +6$" "$run/report"
grep -Eq "^Omitted cases proven: +0$" "$run/report"
grep -Eq "^Function contracts proven: +7$" "$run/report"
grep -Eq "^ +partial correctness only: +6$" "$run/report"
grep -Eq "^Unresolved obligations: +0$" "$run/report"
grep -Eq "^Trusted external axioms: +0$" "$run/report"
grep -Eq "^Trusted solvers: +0$" "$run/report"

output=$("$run/program")
if [ "$output" != "3 2 3 7 4" ]; then
    echo "expected the verified program to print '3 2 3 7 4', got '$output'" >&2
    exit 1
fi

# The claim erases, and only its words do: the `;` stays as an empty statement,
# so an unbraced `if` whose body was the claim still has a body, and the return
# after it is not pulled into the `if`.
if grep -qE '(^|[^_[:alnum:]])contradiction +[A-Za-z_]' "$run/runtime.cpp"; then
    echo 'a claim survived into the erased translation unit' >&2
    exit 1
fi
body=$(awk '/if \(x >= 5u\)$/ { getline following; print following; exit }' "$run/runtime.cpp")
if ! [[ "$body" =~ ^\ +\;$ ]]; then
    echo "the unbraced claim did not erase to an empty statement: '$body'" >&2
    exit 1
fi

# C++ first: where `contradiction` names a type, the same spelling is a
# declaration. It is verified as one, a warning says why it is not a claim, and
# no impossible path is counted.
"$CPPL" "$FIXTURES/contradiction_as_a_cpp_name.cpp" -o "$run/declaration" --cppl-trust-report \
    > "$run/declaration.report" 2> "$run/declaration.err"
grep -q "warning \[cppl-syntax\]: 'contradiction' is also a name in this translation unit" "$run/declaration.err"
grep -Eq "^Impossible paths proven: +0$" "$run/declaration.report"
grep -Eq "^Function contracts proven: +1$" "$run/declaration.report"
if [ "$("$run/declaration")" != "4" ]; then
    echo 'the declaration named like a claim did not keep its C++ meaning' >&2
    exit 1
fi

# The translation unit decides, not the file: a type named only in an included
# header makes the same spelling a declaration too.
"$CPPL" "$FIXTURES/contradiction_named_by_a_header.cpp" -o "$run/header_declaration" --cppl-trust-report \
    > "$run/header_declaration.report" 2> "$run/header_declaration.err"
grep -q "warning \[cppl-syntax\]: 'contradiction' is also a name in this translation unit" \
    "$run/header_declaration.err"
grep -Eq "^Impossible paths proven: +0$" "$run/header_declaration.report"
grep -Eq "^Function contracts proven: +1$" "$run/header_declaration.report"
if [ "$("$run/header_declaration")" != "4" ]; then
    echo 'the declaration named like a claim through a header did not keep its C++ meaning' >&2
    exit 1
fi

echo 'runtime paths claimed not to occur are proven one by one and erase to empty statements'
