#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/enum-cases.XXXXXX")
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/enum_cases.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Proof declarations proven: +6$' "$run/report"
    grep -Eq '^Laws proven: +1$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    test "$("$run/program")" = '37 -1 4'
    ! grep -q 'cases s\|cases t\|unnamed(value)\|choose_one' "$run/runtime.cpp"
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    test "$("$run/erased")" = "$("$run/program")"
done
# Source evolution must invalidate an exhaustive proof, even though the residual
# arm existed before the new enumerator did.
sed 's/one = 1u, alias = one/one = 1u, alias = one, added = 2u/' \
    "$FIXTURES/enum_cases.cpp" > "$run/added.cpp"
if "$CPPL" "$run/added.cpp" -o "$run/added" > "$run/added.log" 2>&1; then
    echo 'a newly added enumerator was silently absorbed' >&2
    exit 1
fi
test ! -e "$run/added"
# The diagnostic names the state that gained no arm, not just that one did.
grep -q "non-exhaustive cases: 'One::added' has no arm" "$run/added.log"
! grep -q PROVEN "$run/added.log"
echo 'enum cases verify, survive erasure, and reject newly added alternatives'
