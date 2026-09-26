#!/usr/bin/env bash
# SPEC: STDMODEL-010, STDMODEL-011, STDMODEL-012, STDMODEL-013, STDMODEL-014, STDMODEL-015
# SPEC: STDMODEL-016, STDMODEL-017, STDMODEL-018, STDMODEL-020, STDMODEL-021, STDMODEL-022
# TRUST.md TCB-LIB-006, TCB-LIB-007, TCB-LIB-008, TCB-LIB-009, TCB-LIB-010
#
# The verified sequence subset (RFC 0020): the accepted twins of every refusal
# in `negative/containers.sh` verify, each claim rests on the library model it
# uses and says so, the program computes what its contracts state, and erased
# it is the same code as the same program written in ordinary C++.
#
# Every standard library the suite runs against compiles this: libc++ on
# macOS, libstdc++ in the Linux GCC job. The model names no library's layout,
# so the same claims verify against both.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
# shellcheck source=../support/equivalence.sh
source "$(dirname "$0")/../support/equivalence.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/containers.XXXXXX")

"$CPPL" -std=c++20 "$FIXTURES/containers.cpp" -o "$run/program" --cppl-trust-report > "$run/report"

expect() {
    if ! grep -Eq "$1" "$run/report"; then
        echo "the trust report does not state: $1" >&2
        cat "$run/report" >&2
        exit 1
    fi
}

expect '^Function contracts proven: +32$'
# SPEC: ARITH-009
# `total / v.size()`, divided only where the vector is not empty.
expect '^Defined operations proven: +1$'
expect '^Unresolved obligations: +0$'
# Bounds passed on, the data pointer's count, the preconditions of the
# capability callees, and `at <= in.size()` where the parser calls
# `skip_digits`.
expect '^Call preconditions proven: +5$'
# SPEC: STDMODEL-018
# Every contract but the pointer-only `zero_prefix` rests on a library model,
# and none of those is assumption-free.
expect '^Library-model-dependent claims: 31$'
expect '^Assumption-free claims: +1$'
expect '^  contract of zero_prefix \(.*containers\.cpp:[0-9]+\), identity [0-9a-f]+$'
expect '^    rests on the std::vector model, in its own contract or body$'
expect '^    rests on the std::basic_string<char> model, in its own contract or body$'
expect '^    rests on the std::span model, in its own contract or body$'
expect '^    rests on the std::array model, in its own contract or body$'
expect '^    rests on the std::vector model, through a verified call it makes$'

output=$("$run/program")
expected=$(printf '%s\n%s' '5 4 3 7 5 4 1 2 0 3 6 9 16 y 6 2 1 6 3 5 6 5' '4 4 2')
if [ "$output" != "$expected" ]; then
    echo "the verified program printed '$output'" >&2
    exit 1
fi

# SPEC: STDMODEL-016, STDMODEL-017, STDMODEL-020, VERIFIED-036
# The accepted twins of the crossings `negative/containers.sh` refuses: an
# element read beside a writable view, written beside a read-only one, a
# refined container handed to a call that only reads it, and an empty vector's
# data pointer over zero elements.
"$CPPL" -std=c++20 "$FIXTURES/container_crossings.cpp" -o "$run/crossings" --cppl-trust-report \
    > "$run/crossings.report"
for line in 'Function contracts proven: +10' 'Unresolved obligations: +0' 'Call preconditions proven: +3'; do
    if ! grep -Eq "^$line\$" "$run/crossings.report"; then
        echo "container_crossings.cpp does not report '$line'" >&2
        cat "$run/crossings.report" >&2
        exit 1
    fi
done
if [ "$("$run/crossings")" != '0 1 0 1 0' ]; then
    echo "container_crossings.cpp printed '$("$run/crossings")'" >&2
    exit 1
fi

# SPEC: STDMODEL-022, ERASE-002
# Erased, the subset is the same code as the program without C++L, at -O0 and
# -O2, in every standard `std::span` exists in.
for standard in c++20 c++23; do
    base="$run/equivalence-$standard"
    "$CPPL" "-std=$standard" "$FIXTURES/equivalence/containers.cpp" -o "$base.cppl" --cppl-trust-report \
        "--cppl-emit-projection=$base.runtime.ii" > "$base.report"
    for line in 'Function contracts proven: +7' 'Unresolved obligations: +0' 'Library-model-dependent claims: 7'; do
        if ! grep -Eq "^$line\$" "$base.report"; then
            echo "the equivalence fixture ($standard) does not report '$line'" >&2
            cat "$base.report" >&2
            exit 1
        fi
    done
    if grep -q '__cppl_' "$base.runtime.ii"; then
        echo "analysis scaffolding reached the runtime program ($standard)" >&2
        exit 1
    fi
    "$CLANG" "-std=$standard" "$FIXTURES/equivalence/containers.reference.cpp" -o "$base.reference"
    erased=$("$base.cppl")
    if [ "$erased" != "5 18 2 6 9 3 5" ] || [ "$("$base.reference")" != "$erased" ]; then
        echo "the erased program ($standard) printed '$erased', unlike its reference" >&2
        exit 1
    fi
    for level in -O0 -O2; do
        assembly "$base$level.cppl" "$CPPL" "-std=$standard" "$level" "$FIXTURES/equivalence/containers.cpp"
        assembly "$base$level.reference" "$CLANG" "-std=$standard" "$level" \
            "$FIXTURES/equivalence/containers.reference.cpp"
        same_code "containers ($standard, $level)" "$base$level.cppl" "$base$level.reference"
    done
done

# SPEC: STDMODEL-018, TUBOUND-002, TUBOUND-006, TUBOUND-012, TUBOUND-014
# Container contracts cross translation units through a verification interface.
# A claim proven through one rests on the imported contract, so it is never free
# of assumptions, and it names every model the other unit's proof used: in the
# imported declaration, and in the other unit's body, which the interface
# records with the contract and every unit after it carries on (TRUST.md
# TCB-LIB-010). `three_listed` and `three_counted` differ only in whether the
# body uses a container, and so do the claims proven through them. The report
# states that the interfaces' provenance is unauthenticated.
units="$run/units"
mkdir -p "$units"
cp "$FIXTURES/cross_tu/sequences.hpp" "$FIXTURES/cross_tu/sequences.cpp" "$FIXTURES/cross_tu/sequences_client.cpp" \
    "$FIXTURES/cross_tu/sequences_middle.hpp" "$FIXTURES/cross_tu/sequences_middle.cpp" "$units/"
(
    cd "$units"
    "$CPPL" -std=c++20 -c sequences.cpp -o sequences.o --cppl-emit-interface=sequences.cppli --cppl-trust-report \
        > sequences.report
    "$CPPL" -std=c++20 -c sequences_middle.cpp -o middle.o --cppl-import-interface=sequences.cppli \
        --cppl-emit-interface=middle.cppli --cppl-trust-report > middle.report
    "$CPPL" -std=c++20 -c sequences_client.cpp -o client.o --cppl-import-interface=sequences.cppli \
        --cppl-import-interface=middle.cppli --cppl-trust-report > client.report
    "$CLANG" sequences.o middle.o client.o -o program
)
for line in 'Function contracts proven: +3' 'Library-model-dependent claims: 2'; do
    grep -Eq "^$line\$" "$units/sequences.report" || { echo "sequences.cpp does not report '$line'" >&2; exit 1; }
done
# The record of each contract names the models its proof rested on, and no
# other: the body that used a vector, and not its twin.
recorded_models() {
    awk -v symbol="$2" '$1 == "entry" { inside = ($2 == symbol) } inside && $1 == "model" { print $3 } $1 == "end" { inside = 0 }' "$1"
}
[ "$(recorded_models "$units/sequences.cppli" 'c:@F@three_listed#')" = 'std::vector%20model' ] ||
    { echo "the record of three_listed does not name the std::vector model" >&2; exit 1; }
[ -z "$(recorded_models "$units/sequences.cppli" 'c:@F@three_counted#')" ] ||
    { echo "the record of three_counted names a model its proof did not use" >&2; exit 1; }
[ "$(recorded_models "$units/middle.cppli" 'c:@F@listed_in_the_middle#')" = 'std::vector%20model' ] ||
    { echo "the middle unit did not carry on the model its import rested on" >&2; exit 1; }
for line in 'Function contracts proven: +4' 'Function contracts imported: +4' 'Assumption-free claims: +0' \
    'Library-model-dependent claims: 3' '      whose proof rests on the std::vector model' \
    "Interface provenance: +unauthenticated; 4 imported contracts are believed on the build's word.*"; do
    grep -Eq "^$line\$" "$units/client.report" || {
        echo "the client does not report '$line'" >&2
        cat "$units/client.report" >&2
        exit 1
    }
done
sed -n '/^Library-model-dependent claims:/,/^$/p' "$units/client.report" > "$units/models.listed"
for listed in '^  contract of through_copy ' '^  contract of through_listed ' '^  contract of through_middle ' \
    '^    rests on the std::vector model, identity [0-9a-f]{16}, through the imported contract of three_listed ' \
    '^    rests on the std::vector model, identity [0-9a-f]{16}, through the imported contract of listed_in_the_middle '; do
    grep -Eq "$listed" "$units/models.listed" || {
        echo "the client does not list '$listed'" >&2
        cat "$units/models.listed" >&2
        exit 1
    }
done
if grep -q 'through_counted' "$units/models.listed"; then
    echo "a claim whose callee's proof used no model was listed as resting on one" >&2
    exit 1
fi
# SPEC: TUBOUND-006, TUBOUND-014 -- that claim rests on the record alone, which
# it names, and like every claim through a record is not free of assumptions.
sed -n '/^Interface-dependent claims:/,/^Interface provenance:/p' "$units/client.report" > "$units/interfaced.listed"
for listed in '^  contract of through_counted ' \
    '^    rests on the contract of three_counted \[c:@F@three_counted#\], imported from sequences\.cppli, entry [0-9a-f]{16}, called in its own body$'; do
    grep -Eq "$listed" "$units/interfaced.listed" || {
        echo "the client does not list '$listed' resting on its record" >&2
        cat "$units/interfaced.listed" >&2
        exit 1
    }
done
if [ "$("$units/program")" != "3 3 3 3" ]; then
    echo "the program built from three units printed '$("$units/program")'" >&2
    exit 1
fi

echo 'every verified container operation rests on its model, runs as stated, and erases to the same code'
