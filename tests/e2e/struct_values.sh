#!/usr/bin/env bash
# SPEC: STORAGE-007, STORAGE-008, CLASS-008, CLASS-011
# TRUST.md TCB-AGGREGATE-001, TCB-AGGREGATE-002
#
# Whole struct values in verified bodies (`fixtures/struct_values.cpp`): a local
# initialized from a parameter, a call or another local; a struct handed by
# reference and by value; a struct returned, assigned, nested, holding arrays
# and refined members, and read through the implicit object. In every supported
# standard each contract is proven with nothing unresolved and no assumption,
# the program prints what was proven, and the erased program, compiled by Clang
# alone, prints the same. Refusals are in negative/struct_values.sh.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/struct-values.XXXXXX")

expected=$'5 100 5 7 100 100 1 2\n9 12 6 3 42 50 6 3\n5 8 9 5 7 1 7'

for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/struct_values.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    for line in 'Function contracts proven: +36' 'Call preconditions proven: +7' 'Unresolved obligations: +0' \
        'Trusted external axioms: +0' 'Trust-dependent claims: +0' 'Unsafe-dependent claims: +0'; do
        if ! grep -Eq "^$line\$" "$run/report"; then
            echo "struct_values ($standard) does not report '$line'" >&2
            cat "$run/report" >&2
            exit 1
        fi
    done

    output=$("$run/program")
    if [ "$output" != "$expected" ]; then
        printf 'struct_values (%s) printed\n%s\nexpected\n%s\n' "$standard" "$output" "$expected" >&2
        exit 1
    fi

    # The runtime program is the source with its contracts blanked: every copy,
    # return and assignment stands as written, and it compiles on its own.
    if grep -q '__cppl_' "$run/runtime.cpp"; then
        echo "analysis scaffolding reached the runtime program ($standard)" >&2
        exit 1
    fi
    grep -q 'Config d = c;' "$run/runtime.cpp"
    grep -q 'Config d = make(7u);' "$run/runtime.cpp"
    grep -q 'd = make(8u);' "$run/runtime.cpp"
    grep -q 'return \*this;' "$run/runtime.cpp"
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    if [ "$("$run/erased")" != "$output" ]; then
        echo "the erased program behaves differently ($standard)" >&2
        exit 1
    fi
done

echo 'whole struct values are initialized, passed, returned and assigned, and the erased program agrees'
