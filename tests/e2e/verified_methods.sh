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

expected=$'4 0 3 0\n3 0 2 9 6\n5 1 1 7 2 6 1 4 3\n1 0 3 1 0 1 7\n8 4 4 0 0 0 6 42 12 0'

for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/verified_methods.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    for line in 'Function contracts proven: +56' 'Call preconditions proven: +13' 'Loop invariants proven: +2' \
        'Loop measures proven: +1' 'Recursive call measures proven: +1' 'Unresolved obligations: +0' \
        'Trusted external axioms: +0' 'Unsafe regions: +2'; do
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

# SPEC: CLASS-010
# Scalar members are disjoint storage beside an empty `[[no_unique_address]]`
# member and a bit-field (C++20 and later, where the attribute is standard).
"$CPPL" -std=c++20 --cppl-trust-report "$FIXTURES/methods_scalar_disjoint.cpp" -o "$run/scalar_disjoint" \
    > "$run/scalar_disjoint.report"
grep -Eq '^Function contracts proven: +1$' "$run/scalar_disjoint.report"
test "$("$run/scalar_disjoint")" = '1 5'

# SPEC: CLASS-011, TUBOUND-002, TUBOUND-003, TUBOUND-006
# Across translation units a member function's contract crosses as a
# function's does (RFC 0017): `methods_cross_tu/counter.cpp` proves and
# records the contracts its header states on the class, `client.cpp` sees only
# the header and the interface, uses each recorded contract with the object's
# places as its arguments and its precondition owed at them, rests each claim
# on the contracts it used, and the two objects link and run. The refused
# halves are in `negative/verified_methods.sh`.
cross="$run/cross_tu"
mkdir -p "$cross"
cp "$FIXTURES"/methods_cross_tu/* "$cross/"
"$CPPL" -std=c++20 -c "$cross/counter.cpp" -o "$cross/counter.o" "--cppl-emit-interface=$cross/counter.cppli" \
    --cppl-trust-report > "$cross/counter.report"
grep -Eq '^Function contracts proven: +3$' "$cross/counter.report"
if [ "$(grep -c '^entry ' "$cross/counter.cppli")" != 3 ]; then
    echo "counter.cpp did not record its three member functions" >&2
    cat "$cross/counter.cppli" >&2
    exit 1
fi
"$CPPL" -std=c++20 -I "$cross" -c "$cross/client.cpp" -o "$cross/client.o" \
    "--cppl-import-interface=$cross/counter.cppli" --cppl-trust-report > "$cross/client.report"
for line in 'Function contracts proven: +2' 'Call preconditions proven: +1' 'Unresolved obligations: +0'; do
    if ! grep -Eq "^$line\$" "$cross/client.report"; then
        echo "the client does not report '$line'" >&2
        cat "$cross/client.report" >&2
        exit 1
    fi
done
grep -q 'rests on the contract of Counter::reset \[c:@S@Counter@F@reset#\], imported from' "$cross/client.report"
grep -q 'rests on the contract of Counter::headroom \[c:@S@Counter@F@headroom#1\], imported from' "$cross/client.report"
"$CLANG" "$cross/counter.o" "$cross/client.o" -o "$cross/program"
if [ "$("$cross/program")" != '0 7' ]; then
    echo "the program linked from both units printed '$("$cross/program")'" >&2
    exit 1
fi
echo 'verified member functions prove their contracts over their objects and erase to the classes as written'
