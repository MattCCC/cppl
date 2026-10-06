#!/usr/bin/env bash
# SPEC: CLASS-008, CLASS-010, CLASS-011, STORAGE-005, STDMODEL-011, STDMODEL-020, REFINE-010
# TRUST.md TCB-AGGREGATE-001, TCB-AGGREGATE-003, TCB-OBJ-008
# Member arrays and objects a reference designates that must be refused.
#
# Each program is a twin of something `fixtures/member_storage.cpp` proves, with
# one thing changed so it would be proven only if the storage model got it
# wrong: an element at a term kept apart from a constant element it may be, two
# references kept apart that may designate one object, the object a reference
# designates handed back at the value it arrived with, a member call's effect
# passed over, an element read past the extent, a refinement of a member, of an
# element or of the whole object not charged, and an element of a std::array a
# pointer designates read without the dereference it is.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/member-storage-negative.XXXXXX")

# refused <name> <diagnostic>: the program read from stdin is refused, writes no
# executable, and says why with the diagnostic named (an extended regular
# expression).
refused() {
    local name="$1" diagnostic="$2"
    cat > "$run/$name.cpp"
    if "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "$name was accepted" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if [ -e "$run/$name" ]; then
        echo "$name was refused, but wrote an executable" >&2
        exit 1
    fi
    if ! grep -Eq -- "$diagnostic" "$run/$name.log"; then
        echo "$name was refused, but not with: $diagnostic" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

false_claim='does not satisfy its contract'
unbounded="element index' is not proven"

# --- member arrays of the implicit object -----------------------------------

# The element written at a term may be element 2 itself.
refused term_kept_apart_from_constant "$false_claim" <<'CPP'
#include <array>
#include <cstddef>
struct Stack {
    std::array<int, 16> items;
    std::size_t size;
    verified int overwrite(std::size_t at, int value)
        expects (at < 16u)
        ensures (result == 7)
    {
        items[2] = 7;
        items[at] = value;
        return items[2];
    }
};
int main() { return 0; }
CPP

# The same in a built-in member array.
refused builtin_term_kept_apart "$false_claim" <<'CPP'
struct Ring {
    unsigned slots[8];
    unsigned head;
    verified unsigned overwrite(unsigned at, unsigned value)
        expects (at < 8u)
        ensures (result == 7u)
    {
        slots[2] = 7u;
        slots[at] = value;
        return slots[2];
    }
};
int main() { return 0; }
CPP

# Element 2, written, may be the element read at a term before.
refused constant_kept_apart_from_term "$false_claim" <<'CPP'
struct Ring {
    unsigned slots[8];
    unsigned head;
    verified unsigned reread(unsigned at)
        expects (at < 8u)
        ensures (result == 0u)
    {
        const unsigned seen = slots[at];
        slots[2] = seen + 1u;
        return slots[at] - seen;
    }
};
int main() { return 0; }
CPP

# `size` may be 16, one past the last element.
refused member_index_past_extent "$unbounded" <<'CPP'
#include <array>
#include <cstddef>
struct Stack {
    std::array<int, 16> items;
    std::size_t size;
    verified void push(int value)
        expects (size <= 16u)
        ensures (true)
    {
        items[size] = value;
    }
};
int main() { return 0; }
CPP

refused builtin_member_index_past_extent "$unbounded" <<'CPP'
struct Ring {
    unsigned slots[8];
    unsigned head;
    verified unsigned newest() const
        ensures (true)
    {
        return slots[head];
    }
};
int main() { return 0; }
CPP

# A std::array member whose element type is written as a refinement states no
# content invariant, so its member functions are refused.
refused refined_std_array_member "std::array does not state" <<'CPP'
#include <array>
type Percent = unsigned where (self <= 100u);
struct Gauges {
    std::array<Percent, 3> levels;
    verified void set(unsigned at, unsigned value)
        expects (at < 3u)
        ensures (true)
    {
        levels[at] = value;
    }
};
int main() { return 0; }
CPP

# What a pointer designates is reached only as the dereference it is: an
# element of its member array is never formed as storage of this body, which
# the call through the pointer would leave holding the value read before it.
refused std_array_member_through_pointer "a std::array that a pointer designates, selected at a term, is not modeled" <<'CPP'
#include <array>
#include <cstddef>
struct Stack {
    std::array<unsigned, 16> items;
    std::size_t size;
    verified unsigned first() const
        ensures (true)
    {
        return items[0];
    }
    verified void set(std::size_t at, unsigned value)
        expects (at < 16u)
        ensures (true)
    {
        items[at] = value;
    }
};
verified unsigned rewrite(Stack* p, std::size_t at)
    expects (readable(p) && writable(p) && at < 16u)
    ensures (result == 0u)
{
    const unsigned before = p->first();
    const unsigned seen = p->items[at];
    p->set(at, seen + 1u);
    return p->items[at] - seen + (before - before);
}
int main() { return 0; }
CPP

# --- objects a reference parameter designates --------------------------------

# Two references may designate one object.
refused two_references_kept_apart "$false_claim" <<'CPP'
struct Counter {
    unsigned hits;
    unsigned misses;
};
verified unsigned both(Counter& a, Counter& b)
    ensures (result == 1u)
{
    a.hits = 1u;
    b.hits = 2u;
    return a.hits;
}
int main() { return 0; }
CPP

# A reference to a scalar may designate a member of the object.
refused member_and_scalar_reference "$false_claim" <<'CPP'
struct Counter {
    unsigned hits;
    unsigned misses;
};
verified unsigned through(Counter& c, unsigned& n)
    ensures (result == 1u)
{
    c.hits = 1u;
    n = 2u;
    return c.hits;
}
int main() { return 0; }
CPP

# The implicit object and an object a reference designates may be one object.
refused receiver_and_reference "$false_claim" <<'CPP'
struct Counter {
    unsigned hits;
    unsigned misses;
    verified unsigned absorb(Counter& other)
        ensures (result == 1u)
    {
        hits = 1u;
        other.hits = 2u;
        return hits;
    }
};
int main() { return 0; }
CPP

# The object is handed back at the value its members hold, not the one it
# arrived with.
refused handed_back_at_entry "$false_claim" <<'CPP'
struct Counter {
    unsigned hits;
    unsigned misses;
};
verified void reset(Counter& c)
    expects (c.hits == 5u)
    ensures (c.hits == 5u)
{
    c.hits = 0u;
}
int main() { return 0; }
CPP

# A member call that may write the object leaves what its contract states.
refused member_call_passed_over "$false_claim" <<'CPP'
#include <array>
#include <cstddef>
struct Stack {
    std::array<int, 16> items;
    std::size_t size;
    verified void push(int value)
        expects (size < 16u)
        ensures (size > 0u && size <= 16u)
    {
        items[size] = value;
        size = size + 1u;
    }
};
verified std::size_t push_one(Stack& s, int value)
    expects (s.size == 0u)
    ensures (result == 0u)
{
    s.push(value);
    return s.size;
}
int main() { return 0; }
CPP

# A member call's precondition is owed of the object as it is.
refused member_call_precondition "call-site precondition for 'pop_two -> Stack::pop' is not proven" <<'CPP'
#include <array>
#include <cstddef>
struct Stack {
    std::array<int, 16> items;
    std::size_t size;
    verified void pop()
        expects (size > 0u && size <= 16u)
        ensures (size < 16u)
    {
        size = size - 1u;
    }
};
verified void pop_two(Stack& s)
    expects (s.size > 0u && s.size <= 16u)
    ensures (true)
{
    s.pop();
    s.pop();
}
int main() { return 0; }
CPP

# A refined member of the object owes its refinement where it is written.
refused refined_member_written "not shown to satisfy refinement type 'Percent'" <<'CPP'
type Percent = unsigned where (self <= 100u);
struct Gauge {
    Percent level;
    unsigned id;
};
verified void set(Gauge& g, unsigned x)
    ensures (true)
{
    g.level = x;
}
int main() { return 0; }
CPP

# A refinement of the whole object is not followed member by member, where a
# write to one member would never be charged the object's predicate: no member
# of it is storage this body writes.
refused refined_object_written "this member's object is not tracked storage of this body" <<'CPP'
struct Range {
    unsigned lo;
    unsigned hi;
};
type Ordered = Range where (self.lo <= self.hi);
verified void invert(Ordered& r)
    ensures (true)
{
    r.lo = 5u;
    r.hi = 1u;
}
int main() { return 0; }
CPP

# Kept as one place, the refined object is still caller storage: a write through
# another reference may reach it, and its member is not read at the value it
# arrived with afterwards.
refused refined_object_stale "$false_claim" <<'CPP'
struct Range {
    unsigned lo;
    unsigned hi;
};
type Ordered = Range where (self.lo <= self.hi);
verified unsigned stale(unsigned& r, const Ordered& other)
    expects (other.lo == 1u)
    ensures (result == 1u)
{
    r = 5u;
    return other.lo;
}
int main() { return 0; }
CPP

# --- a std::array handed by reference ----------------------------------------

refused reference_array_unbounded "$unbounded" <<'CPP'
#include <array>
verified void bump(std::array<unsigned, 10>& counts, unsigned bucket)
    ensures (true)
{
    counts[bucket] = 1u;
}
int main() { return 0; }
CPP

# The element written at a term may be element 0.
refused reference_array_term_and_constant "$false_claim" <<'CPP'
#include <array>
verified void clear_first(std::array<unsigned, 10>& counts, unsigned at)
    expects (at < 10u)
    ensures (counts[0] == 0u)
{
    counts[0] = 0u;
    counts[at] = 1u;
}
int main() { return 0; }
CPP

# --- members an aggregate initializer leaves out -----------------------------

# A member left out is zero, never another value.
refused left_out_claimed_nonzero "$false_claim" <<'CPP'
#include <array>
verified unsigned zero_counts()
    ensures (result == 1u)
{
    std::array<unsigned, 10> counts{};
    return counts[3];
}
int main() { return 0; }
CPP

# A default member initializer, not zero, initializes a member left out.
refused left_out_default_member_initializer "default member initializer" <<'CPP'
struct Limits {
    unsigned low;
    unsigned high = 5u;
};
verified unsigned high_of()
    ensures (result == 0u)
{
    Limits l{1u};
    return l.high;
}
int main() { return 0; }
CPP

# So does one in a nested member left out.
refused nested_default_member_initializer "default member initializer" <<'CPP'
struct Inner {
    unsigned v = 7u;
};
struct Outer {
    unsigned a;
    Inner inner;
};
verified unsigned inner_of()
    ensures (result == 0u)
{
    Outer o{1u};
    return o.inner.v;
}
int main() { return 0; }
CPP

# A class with a constructor of its own runs it where it is value-initialized.
refused left_out_constructor "declares a constructor" <<'CPP'
struct Cell {
    Cell() : v(7u) {}
    unsigned v;
};
struct Row {
    unsigned a;
    Cell cell;
};
verified unsigned cell_of()
    ensures (result == 0u)
{
    Row r{1u};
    return r.cell.v;
}
int main() { return 0; }
CPP

# A refined member left out owes its refinement of zero.
refused left_out_refined "not shown to satisfy refinement type 'Positive'" <<'CPP'
type Positive = unsigned where (self > 0u);
struct Account {
    unsigned id;
    Positive balance;
};
verified unsigned balance_of()
    ensures (true)
{
    Account a{1u};
    return a.balance;
}
int main() { return 0; }
CPP

# A designated element leaves out members that are not the trailing ones.
refused left_out_designated "designated initializer" <<'CPP'
struct Pair {
    unsigned a;
    unsigned b;
};
verified unsigned first_of()
    ensures (result == 2u)
{
    Pair p{.b = 2u};
    return p.a;
}
int main() { return 0; }
CPP

# A scalar member given a braced list beside members left out.
refused left_out_scalar_braces "gives a scalar member a braced list" <<'CPP'
verified unsigned first_of()
    ensures (result == 1u)
{
    unsigned a[3] = {{1u}};
    return a[0];
}
int main() { return 0; }
CPP

# An initializer that leaves out a nested member's braces is not matched to the
# members by position.
refused elided_braces "leaves out a nested member's braces|not initialized by an aggregate initializer" <<'CPP'
struct Pair {
    unsigned a;
    unsigned b;
};
struct Wrapped {
    Pair pair;
    unsigned c;
};
verified unsigned c_of()
    ensures (result == 0u)
{
    Wrapped w{1u, 2u, 3u};
    return w.c;
}
int main() { return 0; }
CPP

refused elided_braces_partial "not initialized by an aggregate initializer" <<'CPP'
struct Pair {
    unsigned a;
    unsigned b;
};
struct Wrapped {
    Pair pair;
    unsigned c;
};
verified unsigned c_of()
    ensures (result == 0u)
{
    Wrapped w{1u};
    return w.c;
}
int main() { return 0; }
CPP

# A local with no initializer holds indeterminate values, never zeros.
refused default_initialized "not initialized by an aggregate initializer" <<'CPP'
#include <array>
verified unsigned first_of()
    ensures (result == 0u)
{
    std::array<unsigned, 3> a;
    return a[0];
}
int main() { return 0; }
CPP

echo 'every false twin of the member storage fixture is refused, each with its reason'
