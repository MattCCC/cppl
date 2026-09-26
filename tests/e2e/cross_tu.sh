#!/usr/bin/env bash
# SPEC: TUBOUND-002, TUBOUND-003, TUBOUND-004, TUBOUND-006, TUBOUND-007, TUBOUND-009, TU-002, TU-003, TUBOUND-001, ABI-001, ERASE-010
# TRUST.md TCB-XTU-001, TCB-XTU-002, TCB-PROV-004, TCB-REPORT-005
#
# Contracts proven in one translation unit, used in others through the
# verification interface the proving unit writes (RFC 0017).
#
# `fixtures/cross_tu/library.cpp` proves the contracts `library.hpp` declares
# and records them; `middle.cpp` proves `doubled` through library's `clamp4`;
# `client.cpp` sees only the headers and both interfaces. In c++17, c++20 and
# c++23:
#
#   - every contract library proves is recorded with its statement, whether it
#     is total, and the trusted law and unsafe block it rests on, and a
#     function with internal linkage is not recorded;
#   - the same unit compiled twice writes the same bytes;
#   - the client's contracts are proven from the recorded ones, and each is
#     reported resting on what the recorded proof rests on, so none that uses
#     another unit is listed as assumption-free, a trusted law and an unsafe
#     block of library are named, and totality crosses as recorded;
#   - the three objects link and run;
#   - library and client compile to exactly the code of their erasures written
#     by hand, and each links with the other's plain C++, so an interface
#     changes nothing that runs and no native ABI.
#
# Every refusal is in tests/negative/cross_tu.sh.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
# shellcheck source=../support/equivalence.sh
source "$(dirname "$0")/../support/equivalence.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/cross-tu.XXXXXX")
# The units are compiled from a copy, so an interface is bound to files this
# run owns.
cp "$FIXTURES"/cross_tu/* "$run/"
cd "$run"

expected='3 2 5 6 3 42 10 10 0 6 4 1'
# Where library.cpp writes its one unsafe block.
block_line=$(grep -n 'unsafe {' library.cpp | cut -d: -f1)
[ "$(printf '%s\n' "$block_line" | wc -l | tr -d ' ')" = 1 ] || { echo "library.cpp should hold one unsafe block" >&2; exit 1; }

fail() {
    echo "$1" >&2
    exit 1
}

for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" -c library.cpp -o "library-$standard.o" \
        "--cppl-emit-interface=library-$standard.cppli" --cppl-trust-report > "library-$standard.report"
    interface="library-$standard.cppli"

    # SPEC: TUBOUND-002 -- what library proved, and nothing it did not.
    grep -Eq '^Function contracts proven: +11$' "library-$standard.report"
    [ "$(grep -c '^entry ' "$interface")" = 10 ] || fail "library recorded $(grep -c '^entry ' "$interface") contracts, not 10"
    for symbol in 'clamp4#i#' 'small_of#i#' 'count_to#i#' 'count_up#i#' 'never_seven#i#' 'sensor#' 'bump#&i#' \
        'step#i#' 'step#l#' 'bound<#Vi4>#i#'; do
        grep -Fqx "entry c:@F@$symbol" "$interface" || fail "library did not record $symbol"
    done
    # SPEC: TUBOUND-004 -- a function with internal linkage is no other unit's.
    if grep -q 'below_four' "$interface"; then
        fail "a function with internal linkage was recorded"
    fi
    # Every entry states status proven; what it rests on is recorded with it.
    [ "$(grep -c '^status proven$' "$interface")" = 10 ] || fail "an entry is not recorded as proven"
    # SPEC: TUBOUND-007 -- the two contracts without an established termination.
    [ "$(grep -c '^correctness partial$' "$interface")" = 2 ] || fail "not exactly two partial contracts recorded"
    awk '/^entry /{e=$2} /^correctness partial$/{print e}' "$interface" | sort > partial.recorded
    printf '%s\n' 'c:@F@count_up#i#' 'c:@F@sensor#' | sort > partial.expected
    diff -u partial.expected partial.recorded
    # SPEC: TUBOUND-006 -- the trusted law and the unsafe block, recorded with the
    # contracts that rest on them and with no other.
    awk '/^entry /{e=$2} /^premise /{print e, $NF}' "$interface" > premises.recorded
    [ "$(cat premises.recorded)" = 'c:@F@never_seven#i# broken_counter' ] || fail "premises recorded: $(cat premises.recorded)"
    awk '/^entry /{e=$2} /^unsafe /{print e, $2}' "$interface" > unsafe.recorded
    [ "$(cat unsafe.recorded)" = "c:@F@sensor# $block_line" ] || fail "unsafe blocks recorded: $(cat unsafe.recorded)"

    # SPEC: TUBOUND-002 -- deterministic: the same unit writes the same bytes.
    "$CPPL" "-std=$standard" -c library.cpp -o "library-again.o" "--cppl-emit-interface=library-again.cppli"
    cmp "$interface" library-again.cppli || fail "two compiles of library wrote different interfaces ($standard)"

    # A unit proven through another unit's contract records that it is.
    "$CPPL" "-std=$standard" -c middle.cpp -o "middle-$standard.o" "--cppl-import-interface=$interface" \
        "--cppl-emit-interface=middle-$standard.cppli"
    grep -Eq '^depends [0-9a-f]{64} c:@F@clamp4#i#$' "middle-$standard.cppli" || fail "middle did not record clamp4"

    "$CPPL" "-std=$standard" -c client.cpp -o "client-$standard.o" "--cppl-import-interface=$interface" \
        "--cppl-import-interface=middle-$standard.cppli" --cppl-trust-report > "client-$standard.report"
    report="client-$standard.report"
    grep -Eq '^Function contracts proven: +12$' "$report"
    grep -Eq '^Function contracts imported: +11$' "$report"
    grep -Eq '^Unresolved obligations: +0$' "$report"
    grep -Eq '^Call preconditions proven: +6$' "$report"

    # SPEC: TUBOUND-006 -- only `own` rests on nothing of another unit, directly
    # or through a verified call it makes.
    sed -n '/^Assumption-free claims:/,/^Unused trusted laws:/p' "$report" > free.listed
    grep -Eq '^Assumption-free claims: +1$' free.listed
    grep -q 'contract of own ' free.listed
    grep -Eq '^Interface-dependent claims: +11$' "$report"
    sed -n '/contract of via_local /,/^  contract of /p' "$report" > via_local.listed
    grep -Eq 'rests on the contract of clamp4 \[c:@F@clamp4#i#\], imported from library-c\+\+[0-9]+\.cppli, entry [0-9a-f]{16}, through a verified call it makes' \
        via_local.listed

    # The trusted law library's proof of never_seven rests on is named, as that
    # law, through the imported contract, and nothing else rests on a law.
    sed -n '/^Trust-dependent claims:/,/^Unsafe-dependent claims:/p' "$report" > trusted.listed
    grep -Eq '^Trust-dependent claims: +1$' trusted.listed
    grep -q 'contract of not_seven ' trusted.listed
    grep -Eq 'rests on broken_counter \(library\.cpp:[0-9]+\), identity [0-9a-f]{16}, through the imported contract of never_seven' trusted.listed

    # The unsafe block of sensor, likewise.
    sed -n '/^Unsafe-dependent claims:/,/^Assumption-free claims:/p' "$report" > unsafe.listed
    grep -Eq '^Unsafe-dependent claims: +1$' unsafe.listed
    grep -q 'contract of reading ' unsafe.listed
    grep -Eq "rests on unsafe block \\(library\\.cpp:$block_line:5\\), through the imported contract of sensor" \
        unsafe.listed

    # SPEC: TUBOUND-007 -- totality crosses as recorded: counted is total through
    # count_to, counted_up and reading are partial through count_up and sensor.
    sed -n '/^Partial-correctness contracts:/,/^Interface-dependent claims:/p' "$report" > partial.listed
    grep -Eq '^Partial-correctness contracts: +2$' partial.listed
    grep -q 'contract of counted_up ' partial.listed
    grep -q 'contract of reading ' partial.listed
    if grep -q 'contract of counted ' partial.listed; then
        fail "counted is total through count_to, and was reported partial"
    fi

    # TRUST.md TCB-PROV-004 -- what middle's proof rested on is carried through.
    grep -Eq 'which rests on the contract of \[c:@F@clamp4#i#\], entry [0-9a-f]{16}' "$report"

    "$CLANG" "library-$standard.o" "middle-$standard.o" "client-$standard.o" -o "program-$standard"
    [ "$("./program-$standard")" = "$expected" ] || fail "the three units ran differently ($standard)"

    # SPEC: ERASE-010, ABI-001 -- the same code as the erasure written by
    # hand, whatever interface was written or read, at both levels.
    for level in -O0 -O2; do
        base="$standard$level"
        assembly "$base.library" "$CPPL" "-std=$standard" "$level" library.cpp \
            "--cppl-emit-interface=$base.library.cppli"
        assembly "$base.library.reference" "$CLANG" "-std=$standard" "$level" library.reference.cpp
        same_code "library ($standard, $level)" "$base.library" "$base.library.reference"
        assembly "$base.client" "$CPPL" "-std=$standard" "$level" client.cpp "--cppl-import-interface=$interface" \
            "--cppl-import-interface=middle-$standard.cppli"
        assembly "$base.client.reference" "$CLANG" "-std=$standard" "$level" client.reference.cpp
        same_code "client ($standard, $level)" "$base.client" "$base.client.reference"
    done

    # Each C++L-compiled unit links with the other's ordinary C++.
    "$CLANG" "-std=$standard" -c library.reference.cpp -o "library-plain-$standard.o"
    "$CLANG" "-std=$standard" -c client.reference.cpp -o "client-plain-$standard.o"
    "$CLANG" "library-plain-$standard.o" "middle-$standard.o" "client-$standard.o" -o "mixed-client-$standard"
    "$CLANG" "library-$standard.o" "middle-$standard.o" "client-plain-$standard.o" -o "mixed-library-$standard"
    [ "$("./mixed-client-$standard")" = "$expected" ] || fail "the C++L client ran differently on plain C++"
    [ "$("./mixed-library-$standard")" = "$expected" ] || fail "the plain client ran differently on C++L"
done

echo "contracts cross translation units through verification interfaces in c++17, c++20 and c++23," \
     "with their trust closure, and change nothing that runs"
