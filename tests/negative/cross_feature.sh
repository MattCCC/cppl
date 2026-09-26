#!/usr/bin/env bash
# SPEC: CLASS-008, CLASS-011, CLASS-015, STDMODEL-015, STDMODEL-019, ARITH-006
#
# The refused twins of tests/e2e/cross_feature.sh, each a written-out file in
# fixtures/negative/cross_feature_*.cpp that differs in one thing from what
# that test shows verifying. Two state false goals: a span read after a member
# call that may reallocate its vector, and a signed sum over a span's elements
# without its guard. One is a combination the implementation refuses, shown
# refused for the reason stated rather than silently accepted: a container
# member of the object a member function runs on.
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

echo 'cross-feature twins fail closed: a stale view, an unguarded sum and a container member'
