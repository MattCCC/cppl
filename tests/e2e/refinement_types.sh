#!/usr/bin/env bash
# Refinement types end to end (SPEC.md 17, 18).
#
# Three properties are checked here: the obligations a refinement creates are
# proven from the program itself, the C++ the declaration lowers to is exactly the
# alias it means, and the erased program compiled by Clang alone behaves the same.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/refinement-types.XXXXXX")

for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/refinement_types.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Laws proven: +1$' "$run/report"
    grep -Eq '^Function contracts proven: +7$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Laws trusted: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    test "$("$run/program")" = '50 1 2 3 0'

    # The canonical lowering, verified as text: a refinement declaration becomes
    # the alias for its base type and nothing else. No wrapper, no predicate, no
    # runtime check.
    grep -Fqx 'using NonNegative = int;' "$run/runtime.cpp"
    grep -Fqx 'using Percentage = NonNegative;' "$run/runtime.cpp"
    grep -Fqx 'using Small = unsigned;' "$run/runtime.cpp"
    grep -Fqx 'template <unsigned n> using Index = unsigned;' "$run/runtime.cpp"
    grep -Fqx 'template <unsigned n> using Short = unsigned;' "$run/runtime.cpp"
    ! grep -q 'where' "$run/runtime.cpp"
    ! grep -q '\bself\b' "$run/runtime.cpp"
    ! grep -q 'struct Percentage\|class Percentage' "$run/runtime.cpp"

    # The program the user gets is the program Clang compiles on its own, and it
    # behaves identically: a refinement changes no runtime representation.
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    test "$("$run/erased")" = "$("$run/program")"
done

# A lowering occupies its declaration's own lines, so nothing below it moves.
# The five declarations are consecutive in the fixture and stay consecutive here,
# which is what keeps a Clang diagnostic on a later line pointing where the
# author wrote it.
first=$(awk '/^using NonNegative = int;$/ { print NR; exit }' "$run/runtime.cpp")
test -n "$first"
for offset in 1 2 3 4; do
    line=$(awk -v n="$((first + offset))" 'NR == n' "$run/runtime.cpp")
    case "$line" in
        using*|template*) ;;
        *)
            echo "a refinement lowering moved the lines below it: '$line'" >&2
            exit 1
            ;;
    esac
done

echo "refinement types verify, lower to their base alias, and run unchanged"
