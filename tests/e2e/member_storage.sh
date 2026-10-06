#!/usr/bin/env bash
# SPEC: CLASS-008, CLASS-010, CLASS-011, STORAGE-005, STDMODEL-011
# TRUST.md TCB-AGGREGATE-001, TCB-AGGREGATE-003
#
# The storage a realistic class holds and a caller hands it
# (`fixtures/member_storage.cpp`): a member array of the implicit object,
# std::array or built in, subscripted at a term; an object a reference parameter
# designates, written member by member and through member calls that may write
# it; and a std::array handed by reference. In every supported standard each
# contract is proven with nothing unresolved and no assumption, the program
# prints what was proven, and the erased program, compiled by Clang alone,
# prints the same. Refusals are in negative/member_storage.sh.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/member-storage.XXXXXX")

expected=$'9 9 2 11 0\n5 2\n2 3 0\n2 0'

for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/member_storage.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report" 2> "$run/err"
    if [ -s "$run/err" ]; then
        cat "$run/err" >&2
        echo "verifying member_storage warned ($standard)" >&2
        exit 1
    fi
    for line in 'Function contracts proven: +14' 'Unresolved obligations: +0' 'Trusted external axioms: +0' \
        'Trust-dependent claims: +0' 'Unsafe-dependent claims: +0'; do
        if ! grep -Eq "^$line\$" "$run/report"; then
            echo "member_storage ($standard) does not report '$line'" >&2
            cat "$run/report" >&2
            exit 1
        fi
    done

    output=$("$run/program")
    if [ "$output" != "$expected" ]; then
        printf 'member_storage (%s) printed\n%s\nexpected\n%s\n' "$standard" "$output" "$expected" >&2
        exit 1
    fi

    # The runtime program is the source with its contracts blanked: every
    # subscript and call stands as written, and it compiles on its own.
    if grep -q '__cppl_' "$run/runtime.cpp"; then
        echo "analysis scaffolding reached the runtime program ($standard)" >&2
        exit 1
    fi
    for written in 'items[size] = value;' 'return items[size - 1u];' 'slots[head] = value;' 's.pop();' \
        'c.hits = 0u;' 'counts[bucket] = counts[bucket] + 1u;'; do
        grep -qF "$written" "$run/runtime.cpp" || {
            echo "'$written' is not in the runtime program ($standard)" >&2
            exit 1
        }
    done
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    if [ "$("$run/erased")" != "$output" ]; then
        echo "the erased program behaves differently ($standard)" >&2
        exit 1
    fi
done

echo 'member arrays and objects a reference designates are followed member by member, and the erased program agrees'
