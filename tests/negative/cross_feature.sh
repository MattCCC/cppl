#!/usr/bin/env bash
# SPEC: CLASS-008, CLASS-010, CLASS-011, CLASS-015, STDMODEL-015, STDMODEL-019, ARITH-006
# SPEC: REFINE-060, REFINE-061, UNSAFE-005, TUBOUND-003
# TRUST.md TCB-OBJ-009
#
# The refused twins of tests/e2e/cross_feature.sh, each a written-out file in
# fixtures/negative/cross_feature_*.cpp that differs in one thing from what
# that test shows verifying. Two state false goals: a span read after a member
# call that may reallocate its vector, and a signed sum over a span's elements
# without its guard. One is a combination the implementation refuses, shown
# refused for the reason stated rather than silently accepted: a container
# member of the object a member function runs on.
#
# The rest try each route by which a refined place a function holds by
# reference could be left counted valid without a charge, where TRUST.md
# TCB-OBJ-009 says it is not: another unit's contract writing a refined member
# through a plain `unsigned&`, an unsafe block inside a loop, one before a
# `break` and one before a `return` inside a loop, and a `push_back` through a
# reference argument that may reach the object. Each could leave a value
# outside the refinement, and each is charged it where it is shown here.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
NEGATIVE="$FIXTURES/negative"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/cross-feature-refused.XXXXXX")
cp "$FIXTURES"/cross_feature/* "$run/"
cd "$run"

fail() {
    echo "$1" >&2
    exit 1
}

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

"$CPPL" -std=c++20 -c buffers.cpp -o buffers.o --cppl-emit-interface=buffers.cppli > /dev/null
"$CPPL" -std=c++20 -c client.cpp -o client.o --cppl-import-interface=buffers.cppli > /dev/null

# SPEC: STDMODEL-015, CLASS-011 -- a member call taking the vector by mutable
# reference ends the span's generation.
refuse span_after_mutating_call "'view' views the storage of 'v', which may have been reallocated or ended by passing it by mutable reference to 'Cursor::emit'" \
    "$NEGATIVE/cross_feature_span_after_mutating_call.cpp" --cppl-import-interface=buffers.cppli
# SPEC: ARITH-006 -- an unguarded element sum may overflow.
refuse unguarded_sum "signed overflow: 'total#2 \\+ value#5' on the signed type 'long long' is not shown to stay within 'long long'" \
    "$NEGATIVE/cross_feature_unguarded_sum.cpp"
# SPEC: CLASS-008, CLASS-015 -- a container member is not tracked storage.
refuse container_member "this member of the implicit object is not tracked storage: its type is not one this implementation models" \
    "$NEGATIVE/cross_feature_container_member.cpp"
# SPEC: STDMODEL-015, CLASS-011 -- a view over a vector that may be the one a
# call grows is stale after it, and the refusal names that call.
refuse alias_view_after_call "'view' views the storage of 'b', which may have been reallocated or ended by passing it by mutable reference to 'grow'" \
    "$NEGATIVE/cross_feature_alias_view_after_call.cpp"

# SPEC: CLASS-011, REFINE-060, TUBOUND-003 -- another unit's contract writing a
# refined member through a plain reference: the caller is charged at the call.
"$CPPL" -std=c++20 -c effects.cpp -o effects.o --cppl-emit-interface=effects.cppli > /dev/null
charged() {
    local name="$1" line="$2" refinement="$3"
    shift 3
    refuse "$name" "cross_feature_$name\\.cpp:$line: error \\[kernel-rejection\\]: this value is not shown to satisfy refinement type '$refinement'" \
        "$NEGATIVE/cross_feature_$name.cpp" "$@"
}
charged imported_unrefined_effect 15:9 Small --cppl-import-interface=effects.cppli
# SPEC: CLASS-010, UNSAFE-005, LOOP-005, REFINE-061 -- the value leaving a loop
# is the one at its head, and an unsafe write inside it is charged at return.
charged loop_unsafe_refined_member 24:6 Small
# SPEC: LOOP-001, REFINE-061 -- a `break` leaves with what the block wrote.
charged loop_break_refined_member 28:6 Small
# SPEC: REFINE-061 -- and so does a `return` inside the loop.
charged loop_return_refined_reference 19:13 Small
# SPEC: CLASS-010, STDMODEL-015 -- a `push_back` through a reference argument
# may reach the object, and nothing establishes its refined member afterwards.
charged refined_receiver_push 20:9 Position

echo 'cross-feature twins fail closed: a stale view, an unguarded sum, a container member, and every route' \
     'that could leave a refined place unchecked'
