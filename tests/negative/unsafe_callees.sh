#!/usr/bin/env bash
# A call to a function whose unsafe code may write what it is handed.
#
# SPEC: UNSAFE-003, UNSAFE-005
# TRUST.md TCB-UNSAFE-004
#
# An unsafe block is not trusted to respect the signature of the function that
# holds it: a block may write through a `const` reference, a pointer to `const`
# or a view of `const` elements, as `const_cast` lets it. A caller therefore
# models a call to a function holding one, directly, through a function it
# calls, or in another unit whose interface records it, as writing every
# reference, pointer and view it hands over, and the callee's contract
# describes each at the value the call leaves there. Each refused program would
# be proven if the caller kept what it knew of a `const` argument; each accepted
# twin is run and prints what was proven.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/unsafe-callees.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

# refuse <name> <diagnostic>: the program is refused for that reason, produces
# no program, and nothing is reported proven.
refuse() {
    local name="$1" diagnostic="$2"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" --cppl-trust-report > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "accepted what must be refused: $name"
    fi
    [ ! -e "$run/$name" ] || fail "$name produced a program"
    if grep -q 'C++L Trust Report' "$run/$name.log"; then
        cat "$run/$name.log" >&2
        fail "$name was reported"
    fi
    grep -qF -- "$diagnostic" "$run/$name.log" || {
        cat "$run/$name.log" >&2
        fail "$name was not refused for the stated reason: $diagnostic"
    }
}

# accept <name> <printed>: the program verifies and prints exactly <printed>.
accept() {
    local name="$1" printed="$2"
    cat > "$run/$name.cpp"
    if ! "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "refused what should verify: $name"
    fi
    local output
    output=$("$run/$name")
    [ "$output" = "$printed" ] || fail "$name printed '$output', not '$printed'"
}

peek='verified unsigned peek(const unsigned& x) ensures (result == x) {
    unsafe {
        const_cast<unsigned&>(x) = 9u;
    }
    return x;
}'

poke='verified void poke(const unsigned& x) ensures (true) {
    unsafe {
        const_cast<unsigned&>(x) = 9u;
    }
}'

# The callee's contract states its result is the argument's value when it
# returns, which its unsafe block changed: the caller's value before the call
# is not that.
refuse result_is_the_value_before <<CPP "return path 'claims_five path 1' does not satisfy its contract"
$peek
verified unsigned claims_five() ensures (result == 5u) {
    unsigned a = 5u;
    return peek(a);
}
CPP

# A `const` argument is not kept across the call.
refuse argument_kept_across_the_call <<CPP "return path 'claims_five path 1' does not satisfy its contract"
$poke
verified unsigned claims_five() ensures (result == 5u) {
    unsigned a = 5u;
    poke(a);
    return a;
}
CPP

# Nor through a verified function that only calls one with unsafe code.
refuse argument_kept_through_a_relay <<CPP "return path 'claims_five path 1' does not satisfy its contract"
$poke
verified void relay(const unsigned& x) ensures (true) {
    poke(x);
}
verified unsigned claims_five() ensures (result == 5u) {
    unsigned a = 5u;
    relay(a);
    return a;
}
CPP

# A temporary is described at the value the call leaves in it, too.
refuse temporary_value_before <<CPP "return path 'claims_successor path 1' does not satisfy its contract"
$peek
verified unsigned claims_successor(unsigned a) expects (a < 100u) ensures (result == a + 1u) {
    return peek(a + 1u);
}
CPP

# A view of `const` elements: the elements are not kept.
refuse const_view_elements <<'CPP' "return path 'claims_five path 1' does not satisfy its contract"
#include <span>
#include <vector>
verified unsigned scribble(std::span<const unsigned> s) expects (s.size() == 1u) ensures (true) {
    unsafe {
        const_cast<unsigned&>(s[0]) = 9u;
    }
    return 0u;
}
verified unsigned claims_five() ensures (result == 5u) {
    std::vector<unsigned> v{5u};
    unsigned ignored = scribble(std::span<const unsigned>(v));
    return v[0] + 0u * ignored;
}
CPP

# A container handed by `const` reference may be grown by the callee's unsafe
# code, ending the life of an element it is handed beside it.
refuse element_beside_const_container <<'CPP' "by a reference through which the callee may reallocate it"
#include <vector>
verified unsigned grow_then_read(const std::vector<unsigned>& w, const unsigned& x) ensures (true) {
    unsafe {
        const_cast<std::vector<unsigned>&>(w).push_back(1u);
    }
    return x;
}
verified unsigned caller() ensures (true) {
    std::vector<unsigned> v{5u};
    return grow_then_read(v, v[0]);
}
CPP

# The elements of a container of refined elements could not be kept valid.
refuse refined_elements_by_const_reference <<'CPP' "nothing obliges the callee to leave only such values in it"
#include <vector>
type Small = unsigned where (self < 10u);
verified unsigned look(const std::vector<unsigned>& w) ensures (true) {
    unsafe {
        const_cast<std::vector<unsigned>&>(w).clear();
    }
    return 0u;
}
verified unsigned caller() ensures (true) {
    std::vector<Small> v{5u};
    return look(v);
}
CPP

# A view of `const` elements over a container of refined elements: the callee's
# unsafe code could break the content invariant through it.
refuse refined_elements_by_const_view <<'CPP' "whose unsafe code may write them, and nothing obliges it to write values satisfying 'Small'"
#include <span>
#include <vector>
type Small = unsigned where (self < 10u);
verified unsigned scribble(std::span<const unsigned> s) ensures (true) {
    unsafe {
        const_cast<unsigned&>(s[0]) = 99u;
    }
    return 0u;
}
verified unsigned caller() ensures (true) {
    std::vector<Small> v{5u};
    return scribble(std::span<const unsigned>(v));
}
CPP

# A const member function's unsafe code may write the object it is called on.
refuse const_member_function_object <<'CPP' "return path 'claims_five path 1' does not satisfy its contract"
struct Counter {
    unsigned value;
    verified unsigned read() const ensures (true) {
        unsafe {
            const_cast<Counter*>(this)->value = 9u;
        }
        return 0u;
    }
};
verified unsigned claims_five() ensures (result == 5u) {
    Counter c{5u};
    unsigned ignored = c.read();
    return c.value + 0u * ignored;
}
CPP

# What such a call writes is followed where the call is a statement, an
# initializer or an assignment of its own. Inside a larger expression or a
# condition it would go unseen, and the argument would read as kept, so the call
# is refused there.
nested="has unsafe code that may write what it is handed, so a call to it requires a statement, initializer or assignment of its own"
refuse call_inside_an_expression <<CPP "'peek' $nested"
$peek
verified unsigned claims_five() ensures (result == 5u) {
    unsigned a = 5u;
    unsigned seen = peek(a) + 1u;
    return a + 0u * seen;
}
CPP

refuse call_in_a_condition <<CPP "'peek' $nested"
$peek
verified unsigned claims_five() ensures (result == 5u) {
    unsigned a = 5u;
    if (peek(a) == 9u) {
        return a;
    }
    return a;
}
CPP

refuse member_call_inside_an_expression <<CPP "'Counter::read' $nested"
struct Counter {
    unsigned value;
    verified unsigned read() const ensures (true) {
        unsafe {
            const_cast<Counter*>(this)->value = 9u;
        }
        return 0u;
    }
};
verified unsigned claims_five() ensures (result == 5u) {
    Counter c{5u};
    unsigned ignored = c.read() + 1u;
    return c.value + 0u * ignored;
}
CPP

# A default argument is evaluated inside the call that relies on it, so a call
# it makes is no statement of its own either.
refuse call_in_a_default_argument <<CPP "'peek' $nested"
$peek
verified unsigned take(unsigned v = peek(3u)) ensures (result == v) { return v; }
verified unsigned relies() ensures (result == 3u) {
    unsigned r = take();
    return r;
}
CPP

# What the caller learns from the callee's contract at the post-state holds.
accept result_from_the_post_state "9" <<CPP
#include <cstdio>
$peek
verified unsigned agrees() ensures (true) {
    unsigned a = 5u;
    return peek(a);
}
int main() {
    std::printf("%u\n", agrees());
    return 0;
}
CPP

# A temporary handed to a function with unsafe code.
accept temporary_handed_over "9" <<CPP
#include <cstdio>
$peek
verified unsigned any_value(unsigned a) expects (a < 100u) ensures (result < 100u || result >= 100u) {
    return peek(a + 1u);
}
int main() {
    std::printf("%u\n", any_value(4u));
    return 0;
}
CPP

# Without unsafe code, a `const` argument keeps its value across the call.
accept const_argument_kept_without_unsafe "5" <<'CPP'
#include <cstdio>
verified unsigned look(const unsigned& x) ensures (result == x) { return x; }
verified unsigned keeps(unsigned a) ensures (result == a) {
    unsigned b = a;
    unsigned seen = look(b);
    return b + 0u * seen;
}
int main() {
    std::printf("%u\n", keeps(5u));
    return 0;
}
CPP

# A call statement whose only temporary is a scalar bound to a `const`
# reference: destroying it runs no code, so the call is modeled
# (SPEC.md STDMODEL-023).
accept scalar_temporary_call_statement "6" <<'CPP'
#include <cstdio>
verified void store(unsigned& out, const unsigned& in) expects (in < 100u) ensures (out < 100u) {
    unsigned value = in;
    out = value;
}
verified unsigned stored(unsigned a) expects (a < 50u) ensures (result < 100u) {
    unsigned o = 0u;
    store(o, a + 1u);
    return o;
}
int main() {
    std::printf("%u\n", stored(5u));
    return 0;
}
CPP

# A function of another unit whose interface records unsafe code it rests on.
producer() {
    local name="$1" body="$2"
    mkdir -p "$run/$name"
    printf '%s\n' 'verified unsigned peek(const unsigned& x) ensures (result == x);' > "$run/$name/peek.hpp"
    printf '%s\n' '#include "peek.hpp"' "verified unsigned peek(const unsigned& x) ensures (result == x) {" "$body" \
        "    return x;" "}" > "$run/$name/peek.cpp"
    "$CPPL" -std=c++20 -c "$run/$name/peek.cpp" -o "$run/$name/peek.o" "--cppl-emit-interface=$run/$name/peek.cppli" \
        > "$run/$name/peek.log" 2>&1 || {
        cat "$run/$name/peek.log" >&2
        fail "the producer $name was refused"
    }
    cat > "$run/$name/client.cpp" <<'CLIENT'
#include <cstdio>
#include "peek.hpp"
verified unsigned claims_five() ensures (result == 5u) {
    unsigned a = 5u;
    unsigned seen = peek(a);
    return a + 0u * seen;
}
int main() {
    std::printf("%u\n", claims_five());
    return 0;
}
CLIENT
}

producer with_unsafe '    unsafe { const_cast<unsigned&>(x) = 9u; }'
if "$CPPL" -std=c++20 "$run/with_unsafe/client.cpp" "$run/with_unsafe/peek.o" -o "$run/with_unsafe/client" \
    "--cppl-import-interface=$run/with_unsafe/peek.cppli" > "$run/with_unsafe/client.log" 2>&1; then
    fail "a caller kept a const argument across a call to another unit's function with unsafe code"
fi
grep -qF "return path 'claims_five path 1' does not satisfy its contract" "$run/with_unsafe/client.log" || {
    cat "$run/with_unsafe/client.log" >&2
    fail "the cross-unit caller was not refused for its contract"
}

producer without_unsafe ''
"$CPPL" -std=c++20 "$run/without_unsafe/client.cpp" "$run/without_unsafe/peek.o" -o "$run/without_unsafe/client" \
    "--cppl-import-interface=$run/without_unsafe/peek.cppli" > "$run/without_unsafe/client.log" 2>&1 || {
    cat "$run/without_unsafe/client.log" >&2
    fail "a caller of another unit's function without unsafe code was refused"
}
[ "$("$run/without_unsafe/client")" = "5" ] || fail "the cross-unit twin did not print what was proven"

echo 'every call to a function whose unsafe code may write what it is handed is modeled as writing it'
