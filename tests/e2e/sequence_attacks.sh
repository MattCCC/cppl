#!/usr/bin/env bash
# SPEC: STDMODEL-012, STDMODEL-013, STDMODEL-015, STDMODEL-016, STDMODEL-018, STDMODEL-021, STDMODEL-025
# SPEC: TUBOUND-004, TUBOUND-006
# TRUST.md TCB-LIB-006, TCB-LIB-010
#
# The accepted twins of every attack `negative/sequence_attacks.sh` refuses
# (RFC 0020): each is proven, rests on the library model it uses and says so,
# and runs to what its contract states, alone and across translation units.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/sequence-attacks.XXXXXX")
fail() {
    echo "$1" >&2
    exit 1
}
expect() {
    grep -Eq "^$2\$" "$1" || { cat "$1" >&2; fail "$(basename "$1") does not state: $2"; }
}

"$CPPL" -std=c++20 "$FIXTURES/sequence_attacks.cpp" -o "$run/program" --cppl-trust-report > "$run/report"
expect "$run/report" 'Function contracts proven: +15'
expect "$run/report" 'Unresolved obligations: +0'
expect "$run/report" 'Loop invariants proven: +2'
# Every contract rests on a library model; the one with an unsafe block rests on
# it too; none is free of assumptions.
expect "$run/report" 'Library-model-dependent claims: 15'
expect "$run/report" 'Unsafe-dependent claims: +1'
expect "$run/report" 'Assumption-free claims: +0'
expect "$run/report" 'Runtime-check-dependent claims: 0'
[ "$("$run/program")" = '6 3 1 1 4 1 2 0 5 1 5 7 1 1 a' ] ||
    fail "the verified program printed '$("$run/program")'"

units="$run/units"
mkdir -p "$units"
cp "$FIXTURES/sequence_attacks_cross_tu/storage.hpp" "$FIXTURES/sequence_attacks_cross_tu/storage.cpp" \
    "$FIXTURES/sequence_attacks_cross_tu/client.cpp" "$units/"
(
    cd "$units"
    "$CPPL" -std=c++20 -c storage.cpp -o storage.o --cppl-emit-interface=storage.cppli --cppl-trust-report \
        > storage.report
    "$CPPL" -std=c++20 -c client.cpp -o client.o --cppl-import-interface=storage.cppli --cppl-trust-report \
        > client.report
    "$CLANG" storage.o client.o -o program
)
expect "$units/storage.report" 'Function contracts proven: +3'
expect "$units/client.report" 'Function contracts proven: +2'
expect "$units/client.report" 'Function contracts imported: +3'
expect "$units/client.report" 'Call preconditions proven: +1'
expect "$units/client.report" 'Assumption-free claims: +0'
[ "$("$units/program")" = '1 5' ] || fail "the program of two units printed '$("$units/program")'"

echo 'every accepted twin of a sequence attack is proven, rests on its model, and runs as stated'
