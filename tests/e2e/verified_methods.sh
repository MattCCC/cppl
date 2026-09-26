#!/usr/bin/env bash
# SPEC: CLASS-008, CLASS-009, CLASS-010, CLASS-011, CLASS-012, CONTRACT-008, CONTRACT-009
# SPEC: CONTRACT-010, CLASS-001, CLASS-003, CONSTRUCT-058
#
# Verified member functions: a statically bound member function is a verified
# callable whose implicit object is storage (`fixtures/verified_methods.cpp`).
# In every supported standard each contract is proven with nothing unresolved,
# the program runs as written, and the runtime program C++L hands to Clang is
# the source with the contracts blanked: every class keeps its members and its
# member functions, and it compiles as ordinary C++ on its own.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/verified-methods.XXXXXX")

expected=$'4 0 3 0\n3 0 2 9 6\n5 1 1 7 2 6 1 4 3\n1 0 3 1 0 1 7'

for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/verified_methods.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    for line in 'Function contracts proven: +39' 'Call preconditions proven: +4' 'Loop invariants proven: +2' \
        'Loop measures proven: +1' 'Recursive call measures proven: +1' 'Unresolved obligations: +0' \
        'Trusted external axioms: +0' 'Unsafe regions: +1'; do
        if ! grep -Eq "^$line\$" "$run/report"; then
            echo "verified_methods ($standard) does not report '$line'" >&2
            cat "$run/report" >&2
            exit 1
        fi
    done
    # Each member function is its own claim, named by its class.
    grep -q 'contract of Counter::after_reset' "$run/report"
    grep -q 'contract of Meter::declared_here' "$run/report"
    grep -q 'contract of Cursor::distance' "$run/report"
    # The one contract resting on an unsafe block is the one whose body holds it.
    grep -q 'contract of Cursor::before_unsafe' "$run/report"

    output=$("$run/program")
    if [ "$output" != "$expected" ]; then
        printf 'verified_methods (%s) printed\n%s\nexpected\n%s\n' "$standard" "$output" "$expected" >&2
        exit 1
    fi

    # The runtime program is ordinary C++: no probe reaches it, the classes and
    # their member functions stand as written, and it compiles on its own.
    if grep -q '__cppl_' "$run/runtime.cpp"; then
        echo "analysis scaffolding reached the runtime program ($standard)" >&2
        exit 1
    fi
    grep -q '    unsigned value;' "$run/runtime.cpp"
    grep -q 'unsigned get() const' "$run/runtime.cpp"
    grep -q 'static  *unsigned add(unsigned a, unsigned b)' "$run/runtime.cpp"
    grep -q 'unsigned take() &&' "$run/runtime.cpp"
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    if [ "$("$run/erased")" != "$output" ]; then
        echo "the erased program behaves differently ($standard)" >&2
        exit 1
    fi
done
echo 'verified member functions prove their contracts over their objects and erase to the classes as written'
