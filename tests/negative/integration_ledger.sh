#!/usr/bin/env bash
# SPEC: STDMODEL-012, STDMODEL-015, STDMODEL-020, REFINE-060, ARITH-006, DEFINEDBEHAVIOR-001, LOOP-002
# SPEC: CASE-004, TRUSTED-002, VERIFIED-045, TUBOUND-003, TUBOUND-005, CLASS-011
#
# The refused twins of the integration fixture set that
# tests/e2e/integration_ledger.sh shows verifying. Each file in
# fixtures/negative/integration_*.cpp is a function of fixtures/integration/
# changed in one thing, and each goal it states is false, so only a verifier
# that let something through could accept it: an index one past the end, an
# amount entering `Money` unguarded, a span read after the vector it views may
# have reallocated, a claim stronger than the imported contract gives, a
# product taken in `int`, a page room without the assumption it rests on, a
# reachable case omitted, digits accumulated without their guard. Two more are
# refused for the stated reason only: a claim written where it cannot be
# checked, and a unit whose interfaces are missing or stale.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
NEGATIVE="$FIXTURES/negative"

# shellcheck source=../support/parallel.sh
source "$(dirname "$0")/../support/parallel.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/integration-refused.XXXXXX")
cp "$FIXTURES"/integration/* "$run/"
cd "$run"

fail() {
    echo "$1" >&2
    exit 1
}

accept() {
    local name="$1"
    shift
    if ! "$CPPL" -std=c++20 -I "$run" -c "$@" -o "$name.o" > "$name.out" 2> "$name.err"; then
        tail -30 "$name.err" >&2
        fail "refused what should verify: $name"
    fi
}

# refuse <name> <pattern> <cppl arguments...>: the compile fails, produces no
# object, reports the reason matching <pattern>, and reports nothing proven.
refuse() {
    local name="$1" pattern="$2"
    shift 2
    if "$CPPL" -std=c++20 -I "$run" -c "$@" -o "$name.o" --cppl-trust-report > "$name.out" 2> "$name.err"; then
        fail "accepted what must be refused: $name"
    fi
    [ ! -e "$name.o" ] || fail "$name produced an object"
    if ! grep -Eq "$pattern" "$name.err"; then
        tail -30 "$name.err" >&2
        fail "$name was refused, but not because: $pattern"
    fi
    if grep -Eq 'Function contracts proven: *[1-9]' "$name.out"; then
        fail "$name reported a contract proven"
    fi
}

# Each compile below is a case of its own, in files of its own, so they run side
# by side (support/parallel.sh), once both interfaces they import are written.
cases_begin "$run/cases"

case_run accept text text.cpp --cppl-emit-interface=text.cppli
case_run accept ledger ledger.cpp --cppl-emit-interface=ledger.cppli
cases_end
both=(--cppl-import-interface=text.cppli --cppl-import-interface=ledger.cppli)
# The accepted twins, with the interfaces the refused ones import.
case_run accept statement statement.cpp "${both[@]}"

# SPEC: STDMODEL-012 -- an index one past the end is not bounded by any
# imported contract.
case_run refuse off_by_one "law 'statement_total element index' is not proven" \
    "$NEGATIVE/integration_off_by_one.cpp" "${both[@]}"
# SPEC: STDMODEL-020, REFINE-060 -- an amount enters a Money element only where
# it is shown to be one.
case_run refuse unguarded_amount "this value is not shown to satisfy refinement type 'Money'" \
    "$NEGATIVE/integration_unguarded_amount.cpp" "${both[@]}"
# SPEC: STDMODEL-015 -- a span formed before a push_back is not read after it.
case_run refuse stale_span "'parsed' views the storage of 'amounts', which may have been reallocated or ended" \
    "$NEGATIVE/integration_stale_span.cpp" "${both[@]}"
# SPEC: TUBOUND-003, CLASS-011 -- an imported member function's contract gives
# what it states and nothing more.
case_run refuse false_room "return path 'room_after path 1' does not satisfy its contract" \
    "$NEGATIVE/integration_false_room.cpp" "${both[@]}"
# SPEC: ARITH-006, DEFINEDBEHAVIOR-001 -- a product of two ints owes that it fits
# an int.
case_run refuse narrow_product "signed overflow: 'quantity#1 \\* unit_price#2' on the signed type 'int'" \
    "$NEGATIVE/integration_narrow_product.cpp"
# SPEC: TRUSTED-002 -- without the trusted assumption, the room may be negative.
case_run refuse untrusted_page "return path 'page_room path 1' does not satisfy its contract" \
    "$NEGATIVE/integration_untrusted_page.cpp"
# SPEC: CASE-004 -- a case the precondition admits is not omitted.
case_run refuse debit_omitted "omitted case 'Direction::debit' of verified function 'directed' is not shown to be impossible" \
    "$NEGATIVE/integration_debit_omitted.cpp"
# SPEC: LOOP-002, ARITH-006 -- digits accumulated without their guard leave the
# bound the invariant states.
case_run refuse unguarded_digits "loop invariant 'read_number loop at line [0-9]+ invariant 4' is not preserved by an iteration" \
    "$NEGATIVE/integration_unguarded_digits.cpp"
# SPEC: VERIFIED-045, CLASS-013 -- a claim in an out-of-line member definition
# is refused, never read as C++ and never assumed.
case_run refuse out_of_line_claim "a claim that a path cannot occur is checked only in a verified function" \
    "$NEGATIVE/integration_out_of_line_claim.cpp"

# SPEC: TUBOUND-003 -- without the ledger's interface, a declaration is no
# evidence of its contract.
case_run refuse missing_interface "'Ledger::line_amount' is declared but not defined in this translation unit, and no imported verification interface records its contract" \
    statement.cpp --cppl-import-interface=text.cppli

# SPEC: TUBOUND-005 -- the tokenizer edited after its interface was written.
stale_interface() {
    mkdir stale
    cp text.hpp text.cpp ledger.hpp statement.cpp text.cppli ledger.cppli stale/
    cd stale
    "$CPPL" -std=c++20 -c text.cpp -o text.o --cppl-emit-interface=text.cppli
    cp "$NEGATIVE/integration_text_changed.cpp" text.cpp
    if "$CPPL" -std=c++20 -c statement.cpp -o statement.o --cppl-import-interface=text.cppli \
        --cppl-import-interface=ledger.cppli 2> stale.err; then
        fail "a stale interface was used"
    fi
    grep -Eq "cannot use verification interface 'text.cppli': it is stale: '.*/stale/text.cpp' has changed since it was produced" stale.err ||
        { cat stale.err >&2; fail "the stale interface was refused for another reason"; }
}
case_run stale_interface
cases_end

echo 'the integration twins fail closed: out of bounds, unguarded, stale, overclaimed, narrow, untrusted,' \
     'omitted, unbounded, unchecked, missing and stale interfaces'
