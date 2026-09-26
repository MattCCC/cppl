#!/usr/bin/env bash
# SPEC: TUBOUND-002, TUBOUND-003, TUBOUND-006, TUBOUND-007, CLASS-008, CLASS-011, REFINE-060, ARITH-006, ARITH-008
# SPEC: STDMODEL-012, STDMODEL-013, STDMODEL-014, STDMODEL-016, STDMODEL-018, STDMODEL-020, CASE-017, TRUSTED-002
# SPEC: ERASE-002, ERASE-010, ABI-001
# TRUST.md TCB-XTU-007, TCB-LIB-010, TCB-REPORT-005
#
# The four features of this release working together in one program of several
# translation units, the shape of a real parser-and-accounting migration:
# `fixtures/integration/text.cpp` tokenizes a span of characters,
# `ledger.cpp` holds money in a class with non-virtual member functions and
# refined members, and `statement.cpp` reads a statement through both units'
# verification interfaces, keeping amounts in a vector of refined elements
# summed under an invariant, with signed arithmetic, a case split, a trusted
# assumption and an unsafe block along the way. In c++20 and c++23:
#
#   - each unit proves what it defines, and its interface records every trusted
#     law, library model and unsafe block each contract rests on;
#   - `statement.cpp` is proven from the imported contracts alone, and its
#     report names, for each claim, the trusted law, the unsafe block and the
#     library models the other units' proofs rested on, so none is
#     assumption-free;
#   - the three objects link and run to the output the contracts describe;
#   - the runtime program each unit's erasure emits builds with plain Clang and
#     runs the same, each unit is the same code as its erasure written by hand,
#     and the hand-erased units run the same whichever compiler builds them.
#
# Every refused twin is in tests/negative/integration_ledger.sh.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
# shellcheck source=../support/equivalence.sh
source "$(dirname "$0")/../support/equivalence.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/integration-ledger.XXXXXX")
# The units are compiled from a copy, so each interface is bound to files this
# run owns.
cp "$FIXTURES"/integration/* "$run/"
cd "$run"

expected='5 lines, 3 amounts: 375 -125 -55; total -55, then 0, room 96'
unsafe_line=$(grep -n 'unsafe {' text.cpp | cut -d: -f1)
[ "$(printf '%s\n' "$unsafe_line" | wc -l | tr -d ' ')" = 1 ] || { echo "text.cpp should hold one unsafe block" >&2; exit 1; }

fail() {
    echo "$1" >&2
    exit 1
}

# reports <file> <extended regex>...: the report states each line.
reports() {
    local file="$1"
    shift
    for line in "$@"; do
        grep -Eq "$line" "$file" || { cat "$file" >&2; fail "$file does not state: $line"; }
    done
}

# The items of one kind an interface records for the entry whose symbol
# contains <name>, one per line.
recorded() {
    awk -v name="$2" -v kind="$3" '
        $1 == "entry" { inside = index($2, name) > 0 }
        inside && $1 == kind { print }
        $1 == "end" { inside = 0 }' "$1"
}

gnu_cxx=""
if command -v g++ > /dev/null 2>&1 && g++ --version 2>/dev/null | grep -q 'Free Software Foundation'; then
    gnu_cxx=g++
fi

for standard in c++20 c++23; do
    text="text-$standard"
    ledger="ledger-$standard"
    statement="statement-$standard"
    "$CPPL" "-std=$standard" -c text.cpp -o "$text.o" "--cppl-emit-interface=$text.cppli" \
        "--cppl-emit-projection=$text.runtime.ii" --cppl-trust-report > "$text.report"
    "$CPPL" "-std=$standard" -c ledger.cpp -o "$ledger.o" "--cppl-emit-interface=$ledger.cppli" \
        "--cppl-emit-projection=$ledger.runtime.ii" --cppl-trust-report > "$ledger.report"
    "$CPPL" "-std=$standard" -c statement.cpp -o "$statement.o" "--cppl-import-interface=$text.cppli" \
        "--cppl-import-interface=$ledger.cppli" "--cppl-emit-projection=$statement.runtime.ii" \
        --cppl-trust-report > "$statement.report"

    # SPEC: TUBOUND-002, STDMODEL-018 -- what each unit proved, and what each
    # record rests on: the span model for every tokenizer, the unsafe block for
    # the setting read, the trusted law for the page, and nothing else.
    reports "$text.report" '^Function contracts proven: +5$' '^Unresolved obligations: +0$' \
        '^Unsafe-dependent claims: +1$' '^Library-model-dependent claims: 4$'
    reports "$ledger.report" '^Function contracts proven: +6$' '^Unresolved obligations: +0$' \
        '^Trust-dependent claims: +4$' '^  contract of Ledger::free_on_page ' '^Omitted cases proven: +1$'
    [ "$(grep -c '^entry ' "$text.cppli")" = 5 ] || fail "text did not record its five contracts"
    [ "$(grep -c '^entry ' "$ledger.cppli")" = 6 ] || fail "ledger did not record its six contracts"
    for name in skip_spaces digits_end line_end read_number; do
        [ "$(recorded "$text.cppli" "@F@$name#" model)" != "" ] || fail "the record of $name names no model"
    done
    [ -z "$(recorded "$text.cppli" '@F@line_limit#' model)" ] || fail "the record of line_limit names a model"
    [ "$(recorded "$text.cppli" '@F@line_limit#' unsafe | cut -d' ' -f2)" = "$unsafe_line" ] ||
        fail "the record of line_limit does not name its unsafe block"
    for name in 'Ledger@F@free_on_page#' '@F@page_room#'; do
        recorded "$ledger.cppli" "$name" premise | grep -q ' page_holds$' || fail "the record of $name names no premise"
    done
    for name in 'Ledger@F@line_amount#' 'Ledger@F@after#' 'Ledger@F@set#' '@F@directed#'; do
        [ -z "$(recorded "$ledger.cppli" "$name" premise)" ] || fail "the record of $name names a premise"
    done

    # SPEC: TUBOUND-003, TUBOUND-006, TUBOUND-007 -- the statement, proven from the
    # imported contracts, rests on everything those proofs rested on.
    report="$statement.report"
    reports "$report" '^Function contracts proven: +5$' '^Function contracts imported: +10$' \
        '^Unresolved obligations: +0$' '^Assumption-free claims: +0$' '^Interface-dependent claims: +4$'
    sed -n '/^Trust-dependent claims:/,/^Unsafe-dependent claims:/p' "$report" > "$statement.trusted"
    reports "$statement.trusted" '^Trust-dependent claims: +1$' '^  contract of room_after ' \
        'rests on page_holds \(ledger\.cpp:[0-9]+\), identity [0-9a-f]{16}, through the imported contract of Ledger::free_on_page '
    sed -n '/^Unsafe-dependent claims:/,/^Assumption-free claims:/p' "$report" > "$statement.unsafe"
    reports "$statement.unsafe" '^Unsafe-dependent claims: +1$' '^  contract of readable_prefix ' \
        "rests on unsafe block \\(text\\.cpp:$unsafe_line:5\\), through the imported contract of line_limit "
    sed -n '/^Library-model-dependent claims:/,/^$/p' "$report" > "$statement.models"
    reports "$statement.models" '^Library-model-dependent claims: 3$' '^  contract of statement_total ' \
        '^  contract of readable_prefix ' '^  contract of count_lines ' \
        '^    rests on the std::vector model, in its own contract or body$' \
        '^    rests on the std::basic_string<char> model, in its own contract or body$' \
        '^    rests on the std::span model, identity [0-9a-f]{16}, through the imported contract of skip_spaces '
    sed -n '/^Partial-correctness contracts:/,/^Interface-dependent claims:/p' "$report" > "$statement.partial"
    reports "$statement.partial" '^Partial-correctness contracts: 1$' '^  contract of readable_prefix '

    "$CLANG" "$text.o" "$ledger.o" "$statement.o" -o "program-$standard"
    [ "$("./program-$standard")" = "$expected" ] || fail "the three units printed '$("./program-$standard")' ($standard)"

    # SPEC: ERASE-002 -- the runtime program each unit's erasure emits is plain
    # C++: nothing of the analysis is in it, Clang alone builds it, and it runs
    # the same.
    for unit in "$text" "$ledger" "$statement"; do
        if grep -q '__cppl_' "$unit.runtime.ii"; then
            fail "analysis scaffolding reached the runtime program of $unit"
        fi
        "$CLANG" "-std=$standard" -x c++ -c "$unit.runtime.ii" -o "$unit.runtime.o"
    done
    "$CLANG" "$text.runtime.o" "$ledger.runtime.o" "$statement.runtime.o" -o "emitted-$standard"
    [ "$("./emitted-$standard")" = "$expected" ] || fail "the emitted runtime programs ran differently ($standard)"

    # SPEC: ERASE-002, ERASE-010, ABI-001 -- each unit is the same code as its
    # erasure written by hand, whatever interface it wrote or read.
    for level in -O0 -O2; do
        base="$standard$level"
        assembly "$base.text" "$CPPL" "-std=$standard" "$level" text.cpp "--cppl-emit-interface=$base.text.cppli"
        assembly "$base.text.reference" "$CLANG" "-std=$standard" "$level" text.reference.cpp
        same_code "text ($standard, $level)" "$base.text" "$base.text.reference"
        assembly "$base.ledger" "$CPPL" "-std=$standard" "$level" ledger.cpp "--cppl-emit-interface=$base.ledger.cppli"
        assembly "$base.ledger.reference" "$CLANG" "-std=$standard" "$level" ledger.reference.cpp
        same_code "ledger ($standard, $level)" "$base.ledger" "$base.ledger.reference"
        assembly "$base.statement" "$CPPL" "-std=$standard" "$level" statement.cpp \
            "--cppl-import-interface=$text.cppli" "--cppl-import-interface=$ledger.cppli"
        assembly "$base.statement.reference" "$CLANG" "-std=$standard" "$level" statement.reference.cpp
        same_code "statement ($standard, $level)" "$base.statement" "$base.statement.reference"
    done

    # The hand-erased units are ordinary C++: they run the same alone, and each
    # C++L-compiled unit links with the others' plain C++.
    for unit in text ledger statement; do
        "$CLANG" "-std=$standard" -c "$unit.reference.cpp" -o "$unit-plain-$standard.o"
    done
    "$CLANG" "text-plain-$standard.o" "ledger-plain-$standard.o" "statement-plain-$standard.o" -o "plain-$standard"
    "$CLANG" "text-plain-$standard.o" "ledger-plain-$standard.o" "$statement.o" -o "mixed-statement-$standard"
    "$CLANG" "$text.o" "$ledger.o" "statement-plain-$standard.o" -o "mixed-units-$standard"
    for program in "plain-$standard" "mixed-statement-$standard" "mixed-units-$standard"; do
        [ "$("./$program")" = "$expected" ] || fail "$program ran differently"
    done
    if [ -n "$gnu_cxx" ]; then
        for unit in text ledger statement; do
            "$gnu_cxx" "-std=$standard" -c "$unit.reference.cpp" -o "$unit-gnu-$standard.o"
        done
        "$gnu_cxx" "text-gnu-$standard.o" "ledger-gnu-$standard.o" "statement-gnu-$standard.o" -o "gnu-$standard"
        [ "$("./gnu-$standard")" = "$expected" ] || fail "the hand-erased units built by GCC ran differently"
    fi
done

if [ -z "$gnu_cxx" ]; then
    echo "GCC is not on this host: the hand-erased units were built with Clang only"
fi
echo "a parser and a ledger across three translation units verify with every trust dependency named," \
     "run as their contracts state, and erase to the same code as ordinary C++"
