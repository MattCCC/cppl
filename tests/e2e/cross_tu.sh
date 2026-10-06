#!/usr/bin/env bash
# SPEC: TUBOUND-002, TUBOUND-003, TUBOUND-004, TUBOUND-006, TUBOUND-007, TUBOUND-009, TUBOUND-012, TUBOUND-014, TU-002, TU-003, TUBOUND-001, ABI-001, ERASE-010
# TRUST.md TCB-XTU-001, TCB-XTU-002, TCB-XTU-007, TCB-XTU-010, TCB-PROV-004, TCB-REPORT-005
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
#     reported resting on the records it was proven through, each listed with
#     its interface, and on what the recorded proofs rest on: a trusted law and
#     an unsafe block of library are named, no claim through a record is free
#     of assumptions, the report states that interface provenance is
#     unauthenticated, and totality crosses as recorded;
#   - the closure units: a claim inherits exactly the trusted laws, models and
#     unsafe blocks of every record it rests on, however many units away;
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
# shellcheck source=../support/parallel.sh
source "$(dirname "$0")/../support/parallel.sh"

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

# Each standard is a case of its own, in a directory of its own, and so are
# the closure units below, so they are checked side by side
# (support/parallel.sh).
in_standard() {
    local standard="$1"
    mkdir "$run/$standard"
    cp "$FIXTURES"/cross_tu/* "$run/$standard/"
    cd "$run/$standard"

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

    # SPEC: TUBOUND-006, TUBOUND-014 -- only `own`, which rests on nothing of
    # another unit, is free of assumptions. Every other claim rests on the
    # records it was proven through, and is listed with each of them and the
    # interface it came from, directly or through a verified call it makes, and
    # with a record the middle unit's rests on; however little a record itself
    # rests on, a claim through it is never assumption-free.
    sed -n '/^Assumption-free claims:/,/^Unused trusted laws:/p' "$report" > free.listed
    grep -Eq '^Assumption-free claims: +1$' free.listed ||
        fail "a claim resting on another unit's record was listed free of assumptions ($standard)"
    grep -q '^  contract of own ' free.listed || fail "own was not listed free of assumptions"
    grep -Eq '^Interface-dependent claims: +11$' "$report"
    sed -n '/^Interface-dependent claims:/,/^Interface provenance:/p' "$report" > interfaced.listed
    if grep -q '^  contract of own ' interfaced.listed; then
        fail "own was listed resting on another unit"
    fi
    grep -Eq '^    rests on the contract of clamp4 \[c:@F@clamp4#i#\], imported from library-c\+\+[0-9]+\.cppli, entry [0-9a-f]{16}, called in its own body$' \
        interfaced.listed
    grep -Eq '^    rests on the contract of clamp4 \[c:@F@clamp4#i#\], imported from library-c\+\+[0-9]+\.cppli, entry [0-9a-f]{16}, through a verified call it makes$' \
        interfaced.listed
    grep -Eq '^    rests on the contract of doubled \[c:@F@doubled#i#\], imported from middle-c\+\+[0-9]+\.cppli, entry [0-9a-f]{16}, called in its own body$' \
        interfaced.listed
    grep -Eq '^      which rests on the contract of \[c:@F@clamp4#i#\], entry [0-9a-f]{16}$' interfaced.listed
    # SPEC: TUBOUND-012 -- nothing authenticates an interface, and the report
    # says so whenever one was imported.
    grep -Eq "^Interface provenance: +unauthenticated; 11 imported contracts are believed on the build's word" "$report" ||
        fail "the report does not state that interface provenance is unauthenticated"
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

    # SPEC: ERASE-010 -- the program each unit hands Clang is the text of its
    # erasure written by hand, token for token.
    "$CPPL" "-std=$standard" -c library.cpp -o "text-library-$standard.o" \
        "--cppl-emit-projection=library-$standard.runtime.ii"
    "$CPPL" "-std=$standard" -c client.cpp -o "text-client-$standard.o" "--cppl-import-interface=$interface" \
        "--cppl-import-interface=middle-$standard.cppli" "--cppl-emit-projection=client-$standard.runtime.ii"
    for unit in library client; do
        tokens "$unit-$standard.runtime.tokens" "$CLANG" "$standard" "$unit-$standard.runtime.ii"
        tokens "$unit-$standard.reference.tokens" "$CLANG" "$standard" "$unit.reference.cpp"
        same_text "$unit ($standard)" "$unit-$standard.runtime.tokens" "$unit-$standard.reference.tokens"
    done

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
}

listed() {
    grep -Eq "$1" "$2" || { cat "$2" >&2; fail "$2 does not state: $1"; }
}

# SPEC: TUBOUND-006, TUBOUND-014 -- a claim through a contract of another unit
# inherits exactly that contract's recorded closure, and is never free of
# assumptions: the claims through `plain`, directly and through the middle unit,
# rest on the records alone, listed with the interfaces they came from, and on
# no trusted law, model or unsafe block; the claims through a callee resting on
# a trusted law, a library model or an unsafe block rest on it too, carried
# however many units away. Each pair differs only in the callee.
closure_units() {
    closure="$run/closure"
    mkdir -p "$closure"
    cp closure.hpp closure.cpp closure_middle.hpp closure_middle.cpp closure_client.cpp "$closure/"
    (
        cd "$closure"
        "$CPPL" -std=c++20 -c closure.cpp -o closure.o --cppl-emit-interface=closure.cppli
        "$CPPL" -std=c++20 -c closure_middle.cpp -o middle.o --cppl-import-interface=closure.cppli \
            --cppl-emit-interface=middle.cppli
        "$CPPL" -std=c++20 -c closure_client.cpp -o client.o --cppl-import-interface=closure.cppli \
            --cppl-import-interface=middle.cppli --cppl-trust-report > client.report
        "$CLANG" closure.o middle.o client.o -o program
    )
    closure_report="$closure/client.report"
    sed -n '/^Assumption-free claims:/,/^Unused trusted laws:/p' "$closure_report" > "$closure/free.listed"
    listed '^Assumption-free claims: +0$' "$closure/free.listed"
    sed -n '/^Interface-dependent claims:/,/^Interface provenance:/p' "$closure_report" > "$closure/interfaced.listed"
    listed '^  contract of through_plain ' "$closure/interfaced.listed"
    listed '^    rests on the contract of plain \[c:@F@plain#i#\], imported from closure\.cppli, entry [0-9a-f]{16}, called in its own body$' \
        "$closure/interfaced.listed"
    listed '^  contract of through_relayed_plain ' "$closure/interfaced.listed"
    listed '^    rests on the contract of relayed_plain \[c:@F@relayed_plain#i#\], imported from middle\.cppli, entry [0-9a-f]{16}, called in its own body$' \
        "$closure/interfaced.listed"
    listed '^      which rests on the contract of \[c:@F@plain#i#\], entry [0-9a-f]{16}$' "$closure/interfaced.listed"
    # The records of `plain` rest on nothing trusted, so neither claim through one
    # is in any list of what a record carries.
    local listing
    for section in 'Trust-dependent claims:/,/^Unsafe-dependent claims:' 'Unsafe-dependent claims:/,/^Assumption-free claims:' \
        'Library-model-dependent claims:/,/^$'; do
        if listing=$(sed -n "/^$section/p" "$closure_report") &&
            grep -Eq '^  contract of through_(relayed_)?plain ' <<< "$listing"; then
            fail "a claim through a record resting on nothing trusted was listed resting on an assumption"
        fi
    done
    sed -n '/^Trust-dependent claims:/,/^Unsafe-dependent claims:/p' "$closure_report" > "$closure/trusted.listed"
    listed '^Trust-dependent claims: +2$' "$closure/trusted.listed"
    listed 'rests on counter_broken \(.*closure\.cpp:[0-9]+\), identity [0-9a-f]{16}, through the imported contract of trusting ' \
        "$closure/trusted.listed"
    listed 'rests on counter_broken \(.*closure\.cpp:[0-9]+\), identity [0-9a-f]{16}, through the imported contract of relayed_trusting ' \
        "$closure/trusted.listed"
    sed -n '/^Unsafe-dependent claims:/,/^Assumption-free claims:/p' "$closure_report" > "$closure/unsafe.listed"
    listed '^Unsafe-dependent claims: +2$' "$closure/unsafe.listed"
    listed 'through the imported contract of unsafe_read ' "$closure/unsafe.listed"
    listed 'through the imported contract of relayed_unsafe ' "$closure/unsafe.listed"
    sed -n '/^Library-model-dependent claims:/,/^$/p' "$closure_report" > "$closure/models.listed"
    listed '^Library-model-dependent claims: 1$' "$closure/models.listed"
    listed '^    rests on the std::vector model, identity [0-9a-f]{16}, through the imported contract of modeled ' \
        "$closure/models.listed"
    listed '^Interface-dependent claims: +7$' "$closure_report"
    [ "$("$closure/program")" = '2 2 2 42 4 4 42' ] || fail "the closure units ran differently"
}

cases_begin "$run/cases"
for standard in c++17 c++20 c++23; do
    case_run in_standard "$standard"
done
case_run closure_units
cases_end

echo "contracts cross translation units through verification interfaces in c++17, c++20 and c++23," \
     "with their trust closure, and change nothing that runs"
