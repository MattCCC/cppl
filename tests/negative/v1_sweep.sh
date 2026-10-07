#!/usr/bin/env bash
# SPEC: LAW-002, INDUCT-002, FORALL-001, EQ-001, REFINE-010, RUNTIMECHECK-011, RUNTIMECHECK-018, STORAGE-005
# SPEC: STORAGE-007, CLASS-008, CLASS-011, EXPR-016, STMT-002, STMT-003, STMT-005, LOOP-002, LOOP-006
# SPEC: TERMINATION-004, TERMINATION-007, CASE-004, VERIFIED-045, ARITH-006, TUBOUND-003, TUBOUND-006
# SPEC: GHOST-002, ERASE-011, UNSAFE-005
# TRUST.md TCB-OBJ-006, TCB-AGGREGATE-003, TCB-UNSAFE-002
#
# The refused twins of tests/e2e/v1_sweep.sh. Each is the program of
# fixtures/v1_sweep/ with one thing changed -- a constant, an operator, a bound
# or a clause -- so that something it claims is false, and each is refused.
#
# A twin is written as the exact text it replaces and the text it puts there,
# in the unit or header that holds it, applied to a fresh copy of the program.
# The text must occur exactly as often as the twin says, once unless stated, so
# a twin can never drift from the program it mutates: when the fixture changes
# under it, the twin fails loudly instead of changing something else or
# nothing. A contract a header states and its definition restates is one
# clause, changed in both; a premise a proof names where it supposes it is
# changed wherever it is named.
#
# The units are compiled in the order a build compiles them, each importing
# the verification interfaces of those before it: `stock.cpp`, `intake.cpp`,
# `desk.cpp`. The units before the one the twin names must still verify. The
# one it names must be refused, write no object and no trust report, and report
# the stated diagnostic at the stated line, so a twin refused for a reason it
# does not state, or at another place, fails here. The line is the mutated one
# where the refusal can stand there; otherwise it is where the change is
# diagnosed: the return a changed contract is owed at, the call a weakened
# guard no longer licenses, the loop whose invariant a changed step breaks.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

# shellcheck source=../support/parallel.sh
source "$(dirname "$0")/../support/parallel.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/v1-sweep-refused.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

# mutate <file>[*<times>] <before> <after>: replaces <before>, which must occur
# exactly <times> times, once unless stated, with <after>.
mutate() {
    local file="${1%\**}" before="$2" after="$3" times=1 text rest count=0
    [ "$file" = "$1" ] || times="${1##*\*}"
    text=$(cat "$file"; printf x)
    text="${text%x}"
    rest="$text"
    while [[ "$rest" == *"$before"* ]]; do
        rest="${rest#*"$before"}"
        count=$((count + 1))
    done
    [ "$count" = "$times" ] || fail "'$before' occurs $count times in $file, not $times"
    printf '%s' "${text//"$before"/"$after"}" > "$file"
}

# compile <unit>: compiles one unit of the program with the interfaces of the
# units before it, writing its own.
compile() {
    local unit="$1" imports=()
    case "$unit" in
        intake) imports=(--cppl-import-interface=stock.cppli) ;;
        desk) imports=(--cppl-import-interface=stock.cppli --cppl-import-interface=intake.cppli) ;;
    esac
    "$CPPL" -std=c++20 -c "$unit.cpp" -o "$unit.o" ${imports[@]+"${imports[@]}"} "--cppl-emit-interface=$unit.cppli" \
        --cppl-trust-report > "$unit.out" 2> "$unit.err"
}

# twin <name> <refused unit> <diagnostic> <file> <line text> <lines after it>
#      {<file>[*<times>] <before> <after>}...
#
# The diagnostic is an extended regular expression for what follows `error `
# on the line of <file> that holds <line text> once the mutations are made, or
# the given number of lines after it.
twin() {
    local name="$1" refused="$2" diagnostic="$3" pinned="$4" text="$5" after="$6" unit lines line
    shift 6
    mkdir "$run/$name"
    cp "$FIXTURES"/v1_sweep/* "$run/$name/"
    cd "$run/$name"
    while [ "$#" -gt 0 ]; do
        mutate "$1" "$2" "$3"
        shift 3
    done
    lines=$(grep -nF -- "$text" "$pinned" | cut -d: -f1)
    [ -n "$lines" ] && [ "$(printf '%s\n' "$lines" | wc -l | tr -d ' ')" = 1 ] ||
        fail "$name: '$text' is not on exactly one line of $pinned"
    line=$((lines + after))
    for unit in stock intake desk; do
        if [ "$unit" != "$refused" ]; then
            compile "$unit" || { tail -30 "$unit.err" >&2; fail "$name: $unit.cpp, which the twin leaves true, was refused"; }
            continue
        fi
        if compile "$unit"; then
            fail "accepted what must be refused: $name"
        fi
        [ ! -e "$unit.o" ] || fail "$name: the refused $unit.cpp wrote an object"
        if grep -q 'C++L Trust Report' "$unit.out" || grep -q 'PROVEN' "$unit.err"; then
            fail "$name: the refused $unit.cpp was reported as proven"
        fi
        grep -Eq "^(\\./)?$pinned:$line:[0-9]+: error $diagnostic" "$unit.err" || {
            tail -30 "$unit.err" >&2
            fail "$name was not refused at $pinned:$line because: $diagnostic"
        }
        return 0
    done
    fail "$name names no unit of the program: $refused"
}

# The program the twins change verifies as it stands, so each refusal below
# is the change's.
accepted() {
    mkdir "$run/accepted"
    cp "$FIXTURES"/v1_sweep/* "$run/accepted/"
    cd "$run/accepted"
    for unit in stock intake desk; do
        compile "$unit" || { tail -30 "$unit.err" >&2; fail "the unchanged program was refused at $unit.cpp"; }
    done
}

# written <twin> <diagnostic> <line text> <interfaces>...: a function of one
# unit written out in fixtures/negative/<twin>.cpp with one check left out,
# compiled beside the accepted program's headers and interfaces, is refused
# at the line holding <line text>.
written() {
    local twin="$1" diagnostic="$2" text="$3" line
    shift 3
    cd "$run/accepted"
    cp "$FIXTURES/negative/$twin.cpp" .
    line=$(grep -nF -- "$text" "$twin.cpp" | cut -d: -f1)
    [ -n "$line" ] && [ "$(printf '%s\n' "$line" | wc -l | tr -d ' ')" = 1 ] ||
        fail "$twin: '$text' is not on exactly one line"
    local imports=()
    for unit in "$@"; do
        imports+=("--cppl-import-interface=$unit.cppli")
    done
    if "$CPPL" -std=c++20 -c "$twin.cpp" -o "$twin.o" "${imports[@]}" --cppl-trust-report > "$twin.out" 2> "$twin.err"; then
        fail "accepted what must be refused: $twin"
    fi
    [ ! -e "$twin.o" ] || fail "$twin wrote an object"
    if grep -q 'C++L Trust Report' "$twin.out" || grep -q 'PROVEN' "$twin.err"; then
        fail "$twin was reported as proven"
    fi
    grep -Eq "^$twin\\.cpp:$line:[0-9]+: error $diagnostic" "$twin.err" || {
        tail -30 "$twin.err" >&2
        fail "$twin was not refused at line $line because: $diagnostic"
    }
}

# What each kind of refusal says.
returned() {
    printf '%s' "\\[kernel-rejection\\]: return path '$1 path $2' does not satisfy its contract"
}
element() {
    printf '%s' "\\[kernel-rejection\\]: law '$1 element index' is not proven"
}
preserved() {
    printf '%s' "\\[kernel-rejection\\]: loop invariant '$1 loop at line [0-9]+ invariant 1' is not preserved by an iteration"
}
refinement="\\[kernel-rejection\\]: this value is not shown to satisfy refinement type 'Quantity'"

# Each twin is a case of its own, in a copy of its own, so they run side by
# side (support/parallel.sh); the written-out twins read the interfaces the
# accepted program writes, so they start once it has.
cases_begin "$run/cases"
case_run accepted
cases_end

# --- Laws, induction, quantifiers, equality and rewriting (stock.cpp) ---------

# SPEC: INDUCT-002, LAW-002 -- two deliveries are not one step: false at zero.
case_run twin induction_step stock \
    "\\[proof-failure\\]: automation does not establish the 'zero' case of induction over 'n'" \
    stock.cpp 'induction n;' 0 \
    stock.cpp 'delivered(start, per, n + 1u) == delivered(start, per, n) + per' \
    'delivered(start, per, n + 2u) == delivered(start, per, n) + per'
# SPEC: FORALL-001, EQ-001 -- delivering one unit at a time moves every start.
case_run twin quantified_equality stock \
    "\\[kernel-rejection\\]: proof 'nothing_delivered_holds' does not establish law 'nothing_delivered'" \
    stock.cpp 'proof nothing_delivered_holds(unsigned n)' 0 \
    stock.cpp 'Eq<unsigned>(delivered(start, 0u, n), start)' 'Eq<unsigned>(delivered(start, 1u, n), start)'
# SPEC: EQ-001, INDUCT-002 -- a total every two steps leave alone may move by
# one: the step the premise gives no longer occurs in the goal, so it rewrites
# nothing.
case_run twin rewrite_premise stock \
    "\\[proof-failure\\]: 'steps' rewrites .*, which does not occur in the goal" \
    stock.cpp 'rewrite steps(pred);' 0 \
    'stock.cpp*3' 'delivered(start, per, m + 1u) == delivered(start, per, m)' \
    'delivered(start, per, m + 2u) == delivered(start, per, m)'
# SPEC: LAW-002, INDUCT-002 -- a steady total stays at its start, not past it.
case_run twin steady_conclusion stock \
    "\\[kernel-rejection\\]: proof 'steady_total_by_induction' does not establish law 'steady_total'" \
    stock.cpp 'proof steady_total_by_induction(' 0 \
    stock.cpp 'proves (delivered(start, per, n) == start);' 'proves (delivered(start, per, n) == start + 1u);' \
    stock.cpp 'delivered(start, per, pred) == start;' 'delivered(start, per, pred) == start + 1u;'

# --- Refinement types and runtime validation (intake.cpp) --------------------

# SPEC: REFINE-010 -- one past a Quantity is not one.
case_run twin refinement_crossing intake "$refinement" intake.cpp 'const Quantity units = requested + 1u;' 0 \
    intake.cpp 'const Quantity units = requested;' 'const Quantity units = requested + 1u;'
# SPEC: RUNTIMECHECK-011, RUNTIMECHECK-018 -- where the validation failed,
# nothing is known of the value.
case_run twin validation_negated intake "$refinement" intake.cpp 'const Quantity units = requested;' 0 \
    intake.cpp 'if (validate<Quantity>(requested)) {' 'if (!validate<Quantity>(requested)) {'

# --- Member arrays, struct values, member calls and mutation ------------------

# SPEC: STORAGE-005, CLASS-008 -- the slot past the top of a std::array member.
case_run twin std_array_member stock "$(element 'Shelf::push')" stock.cpp 'units[size + 1u] = order.units;' 0 \
    stock.cpp 'units[size] = order.units;' 'units[size + 1u] = order.units;'
# SPEC: STORAGE-005, CLASS-008 -- the slot past the top of a built-in member array.
case_run twin builtin_member_array stock "$(element 'Shelf::top')" stock.cpp 'const Order order{ids[size], held};' 0 \
    stock.cpp 'const Order order{ids[size - 1u], held};' 'const Order order{ids[size], held};'
# SPEC: CLASS-011 -- popping a full shelf leaves seven orders.
case_run twin member_postcondition stock "$(returned 'Shelf::pop' 1)" stock.cpp 'size = size - 1u;' 1 \
    stock.hpp 'ensures (size < 8u);' 'ensures (size < 7u);'
# SPEC: STORAGE-007, CLASS-011 -- an aggregate's members are its own, in order.
case_run twin struct_members_swapped stock "$(returned make_order 1)" stock.cpp 'return made;' 0 \
    stock.cpp 'Order made{id, units};' 'Order made{units, id};'
# SPEC: STORAGE-007 -- a whole struct assigned from the smaller order.
case_run twin struct_assignment stock "$(returned larger 1)" stock.cpp 'return chosen;' 0 \
    stock.cpp 'chosen = b;' 'chosen = a;'
# SPEC: CLASS-011, TUBOUND-003 -- after the pop the destination, which may be
# the same shelf, is full when its size is eight, and a push owes room.
case_run twin reference_member_call stock \
    "\\[kernel-rejection\\]: call-site precondition for 'transfer -> Shelf::push' is not proven" \
    stock.cpp 'to.push(moved);' 0 \
    stock.cpp 'if (to.size >= 8u) {' 'if (to.size > 8u) {'
# SPEC: LOOP-002 -- two bookings counted for one leave the count past the
# requests read.
case_run twin counted_twice desk "$(preserved book)" desk.cpp 'for (std::size_t i = 0u; i < requested.size(); ++i)' 0 \
    desk.cpp '++booked;' 'booked += 2u;'

# --- &&, || and ?: as values, conditions and returns (desk.cpp) ---------------

# SPEC: EXPR-016, STORAGE-005 -- the operand reading the element runs at the
# slot one past the top.
case_run twin and_element_bound desk "$(element covers)" desk.cpp 'return slot <= shelf.size && shelf.units[slot]' 0 \
    desk.cpp 'return slot < shelf.size && shelf.units[slot]' 'return slot <= shelf.size && shelf.units[slot]'
# SPEC: EXPR-016 -- an empty shelf is not empty and full at once.
case_run twin or_value desk "$(returned idle_or_full 1)" desk.cpp 'return idle;' 0 \
    desk.cpp 'shelf.size == 0u || shelf.size == kSlots;' 'shelf.size == 0u && shelf.size == kSlots;'
# SPEC: EXPR-016 -- the arm that reads no element answers one, not none.
case_run twin conditional_arm desk "$(returned units_in 2)" desk.cpp 'shelf.units[slot] : 1u;' 0 \
    desk.cpp 'shelf.units[slot] : 0u;' 'shelf.units[slot] : 1u;'
# SPEC: STORAGE-005, ARITH-006 -- level -1 names the element before the ladder.
case_run twin signed_index desk "$(element quote)" desk.cpp 'return ladder[level];' 0 \
    desk.cpp 'expects (0 <= level && level < 4)' 'expects (-1 <= level && level < 4)'

# --- Range-based for, if and switch (intake.cpp, desk.cpp) -------------------

# SPEC: STMT-005, LOOP-002 -- a fifth digit takes the units past four digits.
case_run twin range_for_digits intake "$(preserved parse_units)" intake.cpp 'for (const char c : text)' 0 \
    intake.cpp 'value > 999u' 'value > 9999u'
# SPEC: STMT-005, LOOP-002 -- a running total admitted one unit too high.
case_run twin range_for_total desk "$(preserved requested_total)" desk.cpp 'for (const unsigned units : requested)' 0 \
    desk.cpp 'if (total <= 999000u) {' 'if (total <= 999001u) {'
# SPEC: STMT-002 -- the init-statement counts one order the shelf does not hold.
case_run twin if_init desk "$(returned room_left 1)" desk.cpp 'return kSlots - used;' 0 \
    desk.cpp 'const std::size_t used = shelf.size;' 'const std::size_t used = shelf.size + 1u;'
# SPEC: STMT-003 -- the smallest band's fee is past the bound stated.
case_run twin switch_case desk "$(returned fee_per_unit 1)" desk.cpp 'return 5u;' 0 \
    desk.cpp 'return 4u;' 'return 5u;'

# --- Default arguments, constants and enumerations ---------------------------

# SPEC: TUBOUND-003 -- a default the declaration changes is the one a caller in
# another unit relies on: two units are reserved, not one.
case_run twin default_argument desk "$(returned after_one_sale 1)" desk.cpp 'return reserve(on_hand);' 0 \
    stock.hpp 'unsigned want = 1u)' 'unsigned want = 2u)'
# A shelf of nine slots is not full at eight.
case_run twin constant_global stock "$(returned 'Shelf::full' 1)" stock.cpp 'return size == kSlots;' 0 \
    stock.hpp 'constexpr std::size_t kSlots = 8u;' 'constexpr std::size_t kSlots = 9u;'
# An enumerator moved off the band it labels sends a small order to the
# default, whose fee wraps past the bound.
case_run twin enumerator desk "$(returned fee_per_unit 5)" desk.cpp 'return band - 3u;' 0 \
    desk.cpp 'kSmall = 0u,' 'kSmall = 5u,'

# --- Invariants with ->, contracts with (A -> B) && (C -> D) -----------------

# SPEC: LOOP-002 -- one booking leaves one order, not two.
case_run twin invariant_implication desk "$(preserved book)" desk.cpp 'for (std::size_t i = 0u; i < requested.size(); ++i)' 0 \
    desk.cpp '(booked > 0u -> shelf.size > 0u))' '(booked > 0u -> shelf.size > 1u))'
# SPEC: LOOP-002 -- an order found on the top slot is not below it.
case_run twin found_implication desk "$(preserved slot_of)" desk.cpp 'while (slot < shelf.size && !found)' 0 \
    desk.cpp '(found -> slot < shelf.size))' '(found -> slot + 1u < shelf.size))'
# SPEC: CLASS-011 -- a sale does not move the position up.
case_run twin contract_implications stock "$(returned signed_units 2)" stock.cpp 'return -static_cast<long long>(units);' 0 \
    stock.hpp '(side == Side::sell -> result == -static_cast<long long>(units)));' \
    '(side == Side::sell -> result == static_cast<long long>(units)));' \
    stock.cpp '(side == Side::sell -> result == -static_cast<long long>(units)))' \
    '(side == Side::sell -> result == static_cast<long long>(units)))'

# --- Case splits and impossible paths ----------------------------------------

# SPEC: CASE-004 -- a side of 2 is admitted, so the unnamed case is reachable.
case_run twin omitted_case desk \
    "\\[proof-failure\\]: omitted case 'unnamed' of verified function 'position_after' is not shown to be impossible" \
    desk.cpp 'omit unnamed by contradiction same(0u);' 0 \
    desk.cpp 'units <= 1000u && static_cast<unsigned>(side) <= 1u)' 'units <= 1000u && static_cast<unsigned>(side) <= 2u)'
# SPEC: VERIFIED-045 -- a side of 2 gets past the switch.
case_run twin impossible_path stock \
    "\\[proof-failure\\]: runtime path 'signed_units path 3' is not shown to be unreachable" \
    stock.cpp 'contradiction same(0u);' 0 \
    stock.hpp 'expects (static_cast<unsigned>(side) <= 1u)' 'expects (static_cast<unsigned>(side) <= 2u)' \
    stock.cpp 'expects (static_cast<unsigned>(side) <= 1u)' 'expects (static_cast<unsigned>(side) <= 2u)'

# --- Termination and calls ----------------------------------------------------

# SPEC: TERMINATION-004, TERMINATION-007 -- lots of no units never run out.
case_run twin recursion_measure stock \
    "\\[kernel-rejection\\]: recursive call 'remainder_after_lots -> remainder_after_lots' is not shown to be made at a smaller measure" \
    stock.cpp 'return remainder_after_lots(units - lot, lot);' 0 \
    stock.hpp 'expects (lot > 0u)' 'expects (lot >= 0u)' stock.cpp 'expects (lot > 0u)' 'expects (lot >= 0u)'
# SPEC: LOOP-006 -- the iteration that finds the order raises its measure.
case_run twin loop_measure desk \
    "\\[kernel-rejection\\]: loop measure 'slot_of loop at line [0-9]+ measure' is not shown to decrease on every iteration" \
    desk.cpp 'found ? 1u : 0u)' 0 \
    desk.cpp 'found ? 0u : 1u)' 'found ? 1u : 0u)'
# SPEC: TUBOUND-003 -- lots of no units, asked of another unit's function.
case_run twin call_precondition desk \
    "\\[kernel-rejection\\]: call-site precondition for 'odd_lot -> remainder_after_lots' is not proven" \
    desk.cpp 'remainder_after_lots(units, 0u);' 0 \
    desk.cpp 'remainder_after_lots(units, 250u);' 'remainder_after_lots(units, 0u);'
# SPEC: ARITH-006 -- a position near the largest value overflows.
case_run twin signed_overflow desk \
    "\\[kernel-rejection\\]: an operation in verified function 'position_after' is not shown to have defined behavior: signed overflow" \
    desk.cpp 'return position + signed_units(side, units);' 0 \
    desk.cpp 'position <= 1000000ll' 'position <= 9223372036854775807ll'

# --- Verification interfaces, unsafe code and erasure ------------------------

# SPEC: TUBOUND-003 -- another unit's contract gives an order of up to 1000
# units on top, so the larger of two may be 1000.
case_run twin imported_bound desk "$(returned larger_of_top_two 3)" desk.cpp 'return best.units;' 0 \
    desk.cpp 'ensures (result <= 1000u)' 'ensures (result < 1000u)'
# SPEC: TUBOUND-006 -- every request may be booked, and none of none.
case_run twin booked_bound desk "$(returned book 1)" desk.cpp 'return booked;' 0 \
    desk.cpp 'result <= requested.size() &&' 'result < requested.size() &&'
# SPEC: UNSAFE-005 -- what the unsafe block read is not trusted: nine slots
# from the environment pass a clamp at nine.
case_run twin unsafe_clamp intake "$(returned configured_slots 2)" intake.cpp 'return slots;' 0 \
    intake.cpp 'if (slots > kSlots) {' 'if (slots > kSlots + 1u) {'
# SPEC: GHOST-002, ERASE-011 -- a ghost the program returns would be erased
# from under it.
case_run twin ghost_at_runtime stock "\\[cppl-syntax\\]: ghost 'slots' is used by code that runs" \
    stock.cpp 'return slots;' 0 \
    stock.cpp 'return total;' 'return slots;'

# --- Written-out twins: one check left out of a function of each unit ---------

# SPEC: CLASS-011 -- a push onto a destination that may be full.
case_run written v1_sweep_unchecked_transfer \
    "\\[kernel-rejection\\]: call-site precondition for 'transfer -> Shelf::push' is not proven" 'to.push(moved);' stock
# SPEC: REFINE-010, RUNTIMECHECK-011 -- units from outside entering a Quantity
# unchecked.
case_run written v1_sweep_unvalidated_units "$refinement" 'const Quantity units = requested;' stock
# SPEC: TUBOUND-003 -- another unit's push, made where the shelf may be full.
case_run written v1_sweep_unlimited_booking \
    "\\[kernel-rejection\\]: call-site precondition for 'book -> Shelf::push' is not proven" 'shelf.push(order);' \
    stock intake

cases_end

echo 'every twin of the sweep, one change from a program that verifies, is refused where its change is false'
