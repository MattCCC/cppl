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
    grep -Eq '^Function contracts proven: +18$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Laws trusted: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    test "$("$run/program")" = '50 1 2 3 0 4 1 9'

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

    # A refined data member keeps its declared spelling and gains nothing: the
    # record is the same record, with the same members in the same order and no
    # tag, flag, constructor or check added for the refinement (SPEC.md 17.8).
    grep -Fq 'Percentage level;' "$run/runtime.cpp"
    grep -Fq 'NonNegative count;' "$run/runtime.cpp"

    # The program the user gets is the program Clang compiles on its own, and it
    # behaves identically: a refinement changes no runtime representation.
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    test "$("$run/erased")" = "$("$run/program")"

    # Equal output is necessary and not sufficient: a check that happens to pass
    # on this input leaves the output alone while still being emitted. The claim
    # is that no code exists for the refinement at all, so the comparison is of
    # the object code itself (SPEC.md 17.8, 18).
    #
    # The C++L source and the erased source are each compiled to an object, the
    # first by the C++L driver and the second by Clang alone. Identical objects
    # mean the refinements contributed no instruction, symbol or datum -- no
    # predicate, no tag, no guard on any path, whether or not that path runs
    # here.
    "$CPPL" "-std=$standard" -c "$FIXTURES/refinement_types.cpp" -o "$run/erased_cppl.o"
    "$CLANG" "-std=$standard" -x c++-cpp-output -c "$run/runtime.cpp" -o "$run/erased_clang.o"
    if ! cmp -s "$run/erased_cppl.o" "$run/erased_clang.o"; then
        echo "erasure changed the object code for $standard" >&2
        exit 1
    fi

    # No refinement name reaches the object either. A symbol carrying one would
    # mean the type survived erasure into the runtime image.
    if nm "$run/erased_clang.o" 2>/dev/null | grep -Eq 'Percentage|NonNegative'; then
        echo "a refinement name reached the object code for $standard" >&2
        exit 1
    fi
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
