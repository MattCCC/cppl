#!/usr/bin/env bash
# SPEC: LAW-002, INDUCT-002, FORALL-001, EQ-001, REFINE-010, RUNTIMECHECK-011, RUNTIMECHECK-014, STORAGE-005
# SPEC: STORAGE-007, CLASS-008, CLASS-011, EXPR-016, STMT-002, STMT-003, STMT-005, LOOP-002, LOOP-006
# SPEC: TERMINATION-004, CASE-004, CASE-017, VERIFIED-045, ARITH-006, TUBOUND-002, TUBOUND-003, TUBOUND-006
# SPEC: TUBOUND-014, STDMODEL-018, GHOST-002, ERASE-002, ERASE-011, ERASE-012, UNSAFE-005
# TRUST.md TCB-XTU-010, TCB-LIB-007, TCB-LIB-010, TCB-OBJ-006, TCB-AGGREGATE-003, TCB-REPORT-004
#
# The V1 cross-feature sweep: the mechanisms of the release working together in
# one program of three translation units, the shape of a small trading desk
# (`fixtures/v1_sweep/`).
#
#   - `stock.cpp` proves a stack of orders whose member functions keep the
#     units in a std::array member and the ids in a built-in one, whole order
#     values passed, returned, copied and assigned, a move between two shelves
#     that may be one, a running total under an invariant with a ghost bound,
#     a default argument, a switch whose fall-off path is claimed impossible, a
#     recursion with a measure, signed arithmetic, and Laws about a running
#     total of deliveries proven by induction, with a quantifier, formal
#     equality and rewriting;
#   - `intake.cpp` reads hostile input: digits read by a range-based for, a
#     quantity accepted only through `validate<Quantity>`, a side chosen by a
#     switch on a condition variable, and the desk's configuration read in an
#     unsafe block and clamped;
#   - `desk.cpp` sees the other two only through their headers and the
#     verification interfaces they write: a signed index, `&&`, `||` and `?:`
#     as values and returns with an element read on one arm, an `if`
#     init-statement, a switch labelled by an enumeration, loop invariants
#     stating `->`, a mutating member call through a reference parameter, a
#     value-initialized shelf, a case split whose omitted case is proven
#     impossible, and calls to every function of the other units.
#
# In c++17, c++20 and c++23 every unit verifies with nothing unresolved; its
# report states each PROVEN claim and its whole trust closure: the unsafe block
# the configuration rests on, in its own unit and through the interface, the
# validation site, the library models, and that no claim through an interface
# is assumption-free. The three objects link and run to what the contracts
# state, under each configuration the unsafe block may read, and the runtime
# program each unit's erasure emits builds with plain Clang and runs the same.
#
# Every refused twin is in tests/negative/v1_sweep.sh.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
# shellcheck source=../support/parallel.sh
source "$(dirname "$0")/../support/parallel.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/v1-sweep.XXXXXX")

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

# closure <report>: the report's trust closure, from its trusted laws to its
# runtime validation sites, with what varies from build to build left out:
# identities, USRs, interface entries, and the line each claim is listed at.
closure() {
    sed -E -e 's/, identity [0-9a-f]{16}//g' -e 's/ \[c:[^]]*\]//g' -e 's/entry [0-9a-f]{16}/entry #/g' \
        -e 's/^(  [a-z].*) \((\.\/)?[a-z_]+\.(cpp|hpp):[0-9]+\)$/\1/' "$1" |
        sed -n '/^Trust-dependent claims:/,/^Unverified FFI boundaries:/p'
}

# same_closure <report> <expected closure, on stdin>
same_closure() {
    local report="$1"
    cat > "$report.closure.expected"
    closure "$report" > "$report.closure.actual"
    diff -u "$report.closure.expected" "$report.closure.actual" >&2 || fail "the trust closure of $report differs"
}

# The items of one kind an interface records for the entry whose symbol
# contains <name>, one per line.
recorded() {
    awk -v name="$2" -v kind="$3" '
        $1 == "entry" { inside = index($2, name) > 0 }
        inside && $1 == kind { print }
        $1 == "end" { inside = 0 }' "$1"
}

# What the program prints with the desk configured for `$1` slots; nine is
# more than a shelf holds, so it is clamped to eight, as is no configuration.
expected() {
    case "$1" in
        3)
            printf '%s\n' 'booked 3 of 7, 1376 units accepted, position -622' \
                'slot of 101: 1, units 7, covers 1, room 5, idle or full 0' \
                'shelf holds 377 units; larger of top two 250; restocked 1, 0 left' \
                'fees 4 2 1, quote 103, after a sale 6, odd lot 249, net -2'
            ;;
        *)
            printf '%s\n' 'booked 4 of 7, 1376 units accepted, position -622' \
                'slot of 101: 1, units 7, covers 1, room 4, idle or full 0' \
                'shelf holds 1376 units; larger of top two 999; restocked 1, 1 left' \
                'fees 4 2 1, quote 103, after a sale 6, odd lot 249, net -2'
            ;;
    esac
}

# runs_as_stated <program>: the program prints what its contracts state under
# every configuration the unsafe block may read.
runs_as_stated() {
    local program="$1" slots
    for slots in 3 9; do
        [ "$(DESK_SLOTS="$slots" "$program")" = "$(expected "$slots")" ] ||
            fail "$program printed, with $slots slots configured: $(DESK_SLOTS="$slots" "$program")"
    done
    [ "$(env -u DESK_SLOTS "$program")" = "$(expected 8)" ] ||
        fail "$program printed, with no slots configured: $(env -u DESK_SLOTS "$program")"
}

in_standard() {
    local standard="$1" unit
    mkdir "$run/$standard"
    cp "$FIXTURES"/v1_sweep/* "$run/$standard/"
    cd "$run/$standard"
    local unsafe_line unsafe_function check_line check_column
    unsafe_line=$(grep -n 'unsafe {' intake.cpp | cut -d: -f1)
    unsafe_function=$(grep -n "^unsafe std::size_t slots_from_environment();" intake.cpp | cut -d: -f1)
    check_line=$(grep -n 'validate<Quantity>' intake.cpp | cut -d: -f1)
    check_column=$(grep 'validate<Quantity>' intake.cpp | awk '{ print index($0, "validate<") }')

    "$CPPL" "-std=$standard" -c stock.cpp -o stock.o --cppl-emit-interface=stock.cppli \
        --cppl-emit-projection=stock.runtime.ii --cppl-trust-report > stock.report 2> stock.err
    "$CPPL" "-std=$standard" -c intake.cpp -o intake.o --cppl-import-interface=stock.cppli \
        --cppl-emit-interface=intake.cppli --cppl-emit-projection=intake.runtime.ii --cppl-trust-report \
        > intake.report 2> intake.err
    "$CPPL" "-std=$standard" -c desk.cpp -o desk.o --cppl-import-interface=stock.cppli \
        --cppl-import-interface=intake.cppli --cppl-emit-projection=desk.runtime.ii --cppl-trust-report \
        > desk.report 2> desk.err

    # SPEC: CORRECT-003, CORRECT-005 -- the one warning each unit gives is the
    # partial correctness of what passes through the unsafe block.
    [ ! -s stock.err ] || { cat stock.err >&2; fail "stock.cpp warned ($standard)"; }
    for unit in intake desk; do
        if grep -v '^  ' "$unit.err" | grep -vq 'warning \[partial-correctness\]'; then
            cat "$unit.err" >&2
            fail "$unit.cpp reported something other than partial correctness ($standard)"
        fi
    done
    grep -q "the contract of 'configured_slots' is proven for partial correctness only" intake.err ||
        fail "configured_slots was not warned partial ($standard)"
    grep -q "the contract of 'book' is proven for partial correctness only" desk.err ||
        fail "book was not warned partial ($standard)"

    # SPEC: LAW-002, INDUCT-002, FORALL-001, VERIFIED-045, TERMINATION-004 --
    # what the stock proves, each claim kernel-checked and free of every
    # assumption except the std::array model its shelf's storage rests on.
    reports stock.report '^Laws proven: +3$' '^  by a written proof: +3$' '^Proof declarations proven: +1$' \
        '^Function contracts proven: +12$' '^  partial correctness only: +0$' '^Impossible paths proven: +1$' \
        '^Recursive call measures proven: +1$' '^Loop measures proven: +2$' '^Unresolved obligations: +0$' \
        '^Function contracts imported: +0$'
    same_closure stock.report <<'CLOSURE'
Trust-dependent claims:      0
Unsafe-dependent claims:     0
Assumption-free claims:      13
  law delivery_step
  law nothing_delivered
  law steady_total
  unreachable runtime path signed_units path 3
  proof same
  contract of Shelf::full
  contract of Shelf::pop
  contract of make_order
  contract of larger
  contract of reserve
  contract of signed_units
  contract of net_position
  contract of remainder_after_lots
Unused trusted laws:         0
Library-model-dependent claims: 4
  contract of Shelf::push
    rests on the std::array model, in its own contract or body
  contract of Shelf::top
    rests on the std::array model, in its own contract or body
  contract of transfer
    rests on the std::array model, through a verified call it makes
  contract of shelf_units
    rests on the std::array model, in its own contract or body

Partial-correctness contracts: 0
Interface-dependent claims:  0
Interface provenance:        no verification interface was imported
Runtime-check-dependent claims: 0
Unsafe regions:              0
Runtime validation sites:    0
Unverified FFI boundaries:   not analysed
CLOSURE
    [ "$(grep -c '^entry ' stock.cppli)" = 12 ] || fail "stock did not record its twelve contracts ($standard)"
    [ -n "$(recorded stock.cppli 'Shelf@F@push#' model)" ] || fail "the record of Shelf::push names no model ($standard)"

    # SPEC: RUNTIMECHECK-011, RUNTIMECHECK-014, UNSAFE-005, TCB-REPORT-004 --
    # the intake's one validation is a RUNTIME-CHECKED site, the claim resting
    # on it stays PROVEN, and the configuration rests on the unsafe block.
    reports intake.report '^Function contracts proven: +4$' '^  partial correctness only: +1$' \
        '^  relying on unsafe code: +1$' '^  relying on runtime checks: +1$' '^Unresolved obligations: +0$'
    same_closure intake.report <<CLOSURE
Trust-dependent claims:      0
Unsafe-dependent claims:     1
  contract of configured_slots
    rests on unsafe block (intake.cpp:$unsafe_line:5), in its own body
Assumption-free claims:      1
  contract of accepted_units
Unused trusted laws:         0
Library-model-dependent claims: 2
  contract of parse_units
    rests on the std::basic_string<char> model, in its own contract or body
  contract of side_of
    rests on the std::basic_string<char> model, in its own contract or body

Partial-correctness contracts: 1
  contract of configured_slots
Interface-dependent claims:  0
Interface provenance:        unauthenticated; 12 imported contracts are believed on the build's word (TRUST.md TCB-XTU-010)
Runtime-check-dependent claims: 1
  contract of accepted_units
    rests on the validation of Quantity (intake.cpp:$check_line:$check_column), in its own body
Unsafe regions:              2
  unsafe function:         slots_from_environment (intake.cpp:$unsafe_function:20)
  unsafe block:            intake.cpp:$unsafe_line:5, in verified function configured_slots
Runtime validation sites:    1
  RUNTIME-CHECKED:           intake.cpp:$check_line:$check_column, validates a value against Quantity, where (self <= 1000), in verified function accepted_units
Unverified FFI boundaries:   not analysed
CLOSURE
    [ "$(recorded intake.cppli '@F@configured_slots#' unsafe | cut -d' ' -f2)" = "$unsafe_line" ] ||
        fail "the record of configured_slots does not name its unsafe block ($standard)"
    [ "$(grep -c '^runtime ' intake.cppli)" = 1 ] || fail "the intake records other than its one validation ($standard)"

    # SPEC: TUBOUND-003, TUBOUND-006, TUBOUND-014, STDMODEL-018, CASE-004 --
    # the desk, proven from the imported contracts alone, rests on everything
    # those proofs rested on, and nothing resting on a record is free of
    # assumptions: only what calls nothing is.
    reports desk.report '^Function contracts proven: +14$' '^Function contracts imported: +16$' \
        '^  partial correctness only: +1$' '^  relying on imported contracts: +7$' '^Omitted cases proven: +1$' \
        '^Unresolved obligations: +0$'
    same_closure desk.report <<CLOSURE
Trust-dependent claims:      0
Unsafe-dependent claims:     1
  contract of book
    rests on unsafe block (intake.cpp:$unsafe_line:5), through the imported contract of configured_slots, imported from intake.cppli, entry #
Assumption-free claims:      6
  proof same
  contract of quote
  contract of fee_per_unit
  contract of slot_of
  contract of idle_or_full
  contract of room_left
Unused trusted laws:         0
Library-model-dependent claims: 6
  contract of units_in
    rests on the std::array model, in its own contract or body
  contract of covers
    rests on the std::array model, in its own contract or body
  contract of book
    rests on the std::vector model, in its own contract or body
    rests on the std::array model, through the imported contract of Shelf::push, imported from stock.cppli, entry #
  contract of requested_total
    rests on the std::vector model, in its own contract or body
  contract of restock_from
    rests on the std::array model, through the imported contract of transfer, imported from stock.cppli, entry #
  contract of larger_of_top_two
    rests on the std::array model, through the imported contract of Shelf::top, imported from stock.cppli, entry #

Partial-correctness contracts: 1
  contract of book
Interface-dependent claims:  8
  omitted case 'unnamed' of verified function 'position_after'
    rests on the contract of signed_units, imported from stock.cppli, entry #, called in its own body
  contract of book
    rests on the contract of accepted_units, imported from intake.cppli, entry #, called in its own body
      whose proof rests on the validation of Quantity (intake.cpp:$check_line:$check_column)
    rests on the contract of configured_slots, imported from intake.cppli, entry #, called in its own body
      whose proof rests on unsafe block (intake.cpp:$unsafe_line:5)
    rests on the contract of make_order, imported from stock.cppli, entry #, called in its own body
    rests on the contract of Shelf::push, imported from stock.cppli, entry #, called in its own body
      whose proof rests on the std::array model
  contract of requested_total
    rests on the contract of accepted_units, imported from intake.cppli, entry #, called in its own body
      whose proof rests on the validation of Quantity (intake.cpp:$check_line:$check_column)
  contract of restock_from
    rests on the contract of transfer, imported from stock.cppli, entry #, called in its own body
      whose proof rests on the std::array model
  contract of larger_of_top_two
    rests on the contract of larger, imported from stock.cppli, entry #, called in its own body
    rests on the contract of Shelf::pop, imported from stock.cppli, entry #, called in its own body
    rests on the contract of Shelf::top, imported from stock.cppli, entry #, called in its own body
      whose proof rests on the std::array model
  contract of after_one_sale
    rests on the contract of reserve, imported from stock.cppli, entry #, called in its own body
  contract of odd_lot
    rests on the contract of remainder_after_lots, imported from stock.cppli, entry #, called in its own body
  contract of position_after
    rests on the contract of signed_units, imported from stock.cppli, entry #, called in its own body
Interface provenance:        unauthenticated; 16 imported contracts are believed on the build's word (TRUST.md TCB-XTU-010)
Runtime-check-dependent claims: 2
  contract of book
    rests on the validation of Quantity (intake.cpp:$check_line:$check_column), through the imported contract of accepted_units, imported from intake.cppli, entry #
  contract of requested_total
    rests on the validation of Quantity (intake.cpp:$check_line:$check_column), through the imported contract of accepted_units, imported from intake.cppli, entry #
Unsafe regions:              0
Runtime validation sites:    0
Unverified FFI boundaries:   not analysed
CLOSURE

    # The three objects link and run to what the contracts state.
    "$CLANG" stock.o intake.o desk.o -o program
    runs_as_stated ./program

    # SPEC: ERASE-002, ERASE-011, ERASE-012 -- the runtime program each unit's
    # erasure emits is plain C++: no clause, ghost or claim is left in it, the
    # validation is the validator its refinement lowers to, every runtime
    # check and the unsafe block's code stand as written, and Clang alone
    # builds it to a program that runs the same.
    for unit in stock intake desk; do
        if grep -Eq '^[[:space:]]*(ensures|expects|invariant|decreases|proves|ghost|cases|omit|contradiction|induction|law|proof)[[:space:]]' \
            "$unit.runtime.ii"; then
            fail "proof-only text reached the runtime program of $unit ($standard)"
        fi
        if grep -q 'validate<' "$unit.runtime.ii"; then
            fail "a validation reached the runtime program of $unit unlowered ($standard)"
        fi
    done
    for unit in stock desk; do
        if grep -q '__cppl_' "$unit.runtime.ii"; then
            fail "analysis scaffolding reached the runtime program of $unit ($standard)"
        fi
    done
    [ "$(grep -c '__cppl_' intake.runtime.ii)" = 2 ] || fail "the intake's erasure holds more than its validator ($standard)"
    for written in 'if (__cppl_v_Quantity (requested)) {' 'slots = slots_from_environment();' 'if (slots > kSlots) {' \
        'if (c < '"'0'"' || c > '"'9'"' || value > 999u) {' 'switch (const char code = text[0]) {'; do
        grep -qF -- "$written" intake.runtime.ii || fail "'$written' is not in the intake's runtime program ($standard)"
    done
    for written in 'units[size] = order.units;' 'ids[size] = order.id;' 'chosen = b;' 'to.push(moved);' \
        'total = total + (shelf.units[slot] > kMaxUnits ? kMaxUnits : shelf.units[slot]);'; do
        grep -qF -- "$written" stock.runtime.ii || fail "'$written' is not in the stock's runtime program ($standard)"
    done
    for written in 'return slot < shelf.size && shelf.units[slot] >= wanted;' 'Shelf fresh{};' 'shelf.push(order);' \
        'if (const std::size_t used = shelf.size; used < kSlots) {' 'switch (const unsigned band = units / 250u) {' \
        'return position + signed_units(side, units);' 'for (const unsigned units : requested)'; do
        grep -qF -- "$written" desk.runtime.ii || fail "'$written' is not in the desk's runtime program ($standard)"
    done
    if grep -q 'slots == shelf.size' stock.runtime.ii; then
        fail "the ghost bound reached the stock's runtime program ($standard)"
    fi
    for unit in stock intake desk; do
        "$CLANG" "-std=$standard" -x c++ -c "$unit.runtime.ii" -o "$unit.runtime.o"
    done
    "$CLANG" stock.runtime.o intake.runtime.o desk.runtime.o -o emitted
    runs_as_stated ./emitted

    # Each C++L-compiled unit links with the others' erasure built by Clang.
    "$CLANG" stock.runtime.o intake.o desk.runtime.o -o mixed
    runs_as_stated ./mixed
}

# Each standard is a case of its own, in a copy of its own, so the three are
# checked side by side (support/parallel.sh).
cases_begin "$run/cases"
for standard in c++17 c++20 c++23; do
    case_run in_standard "$standard"
done
cases_end

echo 'a trading desk of three translation units verifies in c++17, c++20 and c++23 with its whole trust closure' \
     'named, runs as its contracts state, and erases to plain C++ that runs the same'
