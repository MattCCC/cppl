#!/usr/bin/env bash
# SPEC: CLASS-008, CLASS-011, STDMODEL-012, STDMODEL-013, STDMODEL-015, STDMODEL-016, STDMODEL-018
# SPEC: ARITH-006, ARITH-008, TUBOUND-002, TUBOUND-003, TUBOUND-006, CLASS-010, REFINE-060, REFINE-062
# TRUST.md TCB-XTU-007, TCB-LIB-010, TCB-REPORT-005, TCB-OBJ-009
#
# The combinations of member functions, the sequence models, signed
# arithmetic and verification interfaces that tests/e2e/integration_ledger.sh
# does not reach: `fixtures/cross_feature/buffers.cpp` proves member functions
# of a cursor that read a span, push to a vector, read a vector by `const&` and
# a string, add signed values, and read a setting in an unsafe block, and a
# function taking a string by value; `client.cpp` uses each through the
# interface. The client's claims each rest on the models and the unsafe block
# the other unit's proofs used, a span survives a `const&` member call, and a
# guarded signed sum over a span's elements is proven. The two objects link
# and run to what the contracts state.
#
# The refused twins are in tests/negative/cross_feature.sh.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/cross-feature.XXXXXX")
cp "$FIXTURES"/cross_feature/* "$run/"
cd "$run"

fail() {
    echo "$1" >&2
    exit 1
}

reports() {
    local file="$1"
    shift
    for line in "$@"; do
        grep -Eq "$line" "$file" || { cat "$file" >&2; fail "$file does not state: $line"; }
    done
}

unsafe_line=$(grep -n 'unsafe {' buffers.cpp | cut -d: -f1)

for standard in c++20 c++23; do
    "$CPPL" "-std=$standard" -c buffers.cpp -o "buffers-$standard.o" "--cppl-emit-interface=buffers-$standard.cppli" \
        --cppl-trust-report > "buffers-$standard.report"
    reports "buffers-$standard.report" '^Function contracts proven: +7$' '^Unresolved obligations: +0$' \
        '^Unsafe-dependent claims: +1$' '^Library-model-dependent claims: 5$'

    "$CPPL" "-std=$standard" -c client.cpp -o "client-$standard.o" "--cppl-import-interface=buffers-$standard.cppli" \
        --cppl-trust-report > "client-$standard.report"
    report="client-$standard.report"
    reports "$report" '^Function contracts proven: +7$' '^Function contracts imported: +7$' \
        '^Unresolved obligations: +0$' '^Interface-dependent claims: +6$'
    # SPEC: TUBOUND-006, TUBOUND-014 -- signed arithmetic through another unit's
    # member function rests on its record alone, listed with it, and on no
    # trusted law, model or unsafe block; resting on a record, it is never free
    # of assumptions.
    sed -n '/^Assumption-free claims:/,/^Unused trusted laws:/p' "$report" > "free-$standard.listed"
    reports "free-$standard.listed" '^Assumption-free claims: +0$'
    sed -n '/^Interface-dependent claims:/,/^Interface provenance:/p' "$report" > "interfaced-$standard.listed"
    reports "interfaced-$standard.listed" '^  contract of moved_twice ' \
        '^    rests on the contract of Cursor::moved \[.*\], imported from buffers-c\+\+[0-9]+\.cppli, entry [0-9a-f]{16}, called in its own body$'
    sed -n '/^Trust-dependent claims:/,/^Assumption-free claims:/p' "$report" > "trusted-$standard.listed"
    if grep -q '^  contract of moved_twice ' "trusted-$standard.listed"; then
        fail "moved_twice rests on no trusted law or unsafe block, and was listed resting on one"
    fi

    # SPEC: TUBOUND-006 -- the unsafe block of another unit's member function.
    sed -n '/^Unsafe-dependent claims:/,/^Assumption-free claims:/p' "$report" > "unsafe-$standard.listed"
    reports "unsafe-$standard.listed" '^Unsafe-dependent claims: +1$' '^  contract of configured ' \
        "rests on unsafe block \\(buffers\\.cpp:$unsafe_line:5\\), through the imported contract of Cursor::setting "

    # SPEC: STDMODEL-018, TUBOUND-006 -- every model a claim rests on, its own
    # and those the other unit's proofs used, including a string's.
    sed -n '/^Library-model-dependent claims:/,/^$/p' "$report" > "models-$standard.listed"
    reports "models-$standard.listed" '^Library-model-dependent claims: 5$' '^  contract of first_copied ' \
        '^  contract of view_after_const_call ' '^  contract of second_character ' '^  contract of suffixed_pair ' \
        '^  contract of guarded_sum ' \
        '^    rests on the std::span model, identity [0-9a-f]{16}, through the imported contract of Cursor::peek ' \
        '^    rests on the std::vector model, identity [0-9a-f]{16}, through the imported contract of Cursor::emit ' \
        '^    rests on the std::basic_string<char> model, identity [0-9a-f]{16}, through the imported contract of suffixed_length ' \
        '^    rests on the std::basic_string<char> model, in its own contract or body$'
    if grep -Eq '^  contract of (moved_twice|configured) ' "models-$standard.listed"; then
        fail "a claim that uses no container was listed as resting on a model"
    fi

    "$CLANG" "buffers-$standard.o" "client-$standard.o" -o "program-$standard"
    [ "$("./program-$standard")" = '8 1 6 y 14 64 3 5' ] || fail "the program printed '$("./program-$standard")'"
done

# SPEC: CLASS-011, REFINE-060, REFINE-062, TUBOUND-003 -- a refined place a
# function holds by reference gets a new value by routes each charged where it
# is established: the effect of another unit's contract on a member of the
# implicit object and on a reference parameter, charged at the call, and a loop,
# whose head value the return is charged and the invariant shows (TRUST.md
# TCB-OBJ-009). The refused twins are in tests/negative/cross_feature.sh.
"$CPPL" -std=c++20 -c effects.cpp -o effects.o --cppl-emit-interface=effects.cppli --cppl-trust-report \
    > effects.report
reports effects.report '^Function contracts proven: +2$' '^Unresolved obligations: +0$'
"$CPPL" -std=c++20 -c effects_client.cpp -o effects_client.o --cppl-import-interface=effects.cppli \
    --cppl-trust-report > effects_client.report
reports effects_client.report '^Function contracts proven: +4$' '^Function contracts imported: +2$' \
    '^Unresolved obligations: +0$' '^Interface-dependent claims: +3$' '^  contract of Gauge::assign_five ' \
    '^  contract of Gauge::settle ' '^  contract of through_reference '
"$CLANG" effects.o effects_client.o -o effects_program
[ "$(./effects_program)" = 5 ] || fail "the effects program printed '$(./effects_program)'"

echo 'member functions over spans, vectors and strings, signed arithmetic over their values, and an unsafe' \
     'dependency cross units with every trust dependency named; a refined place is charged on every route'
