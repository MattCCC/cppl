// Verified member functions (SPEC.md 11.9, CLASS-008 to CLASS-015).
//
// A statically bound member function is a verified callable whose implicit
// object is storage: each scalar member of the object is a place, passed as a
// reference parameter standing before the written ones. A body reads and writes
// members through the one read and write path, a write owes the member's
// refinement, and a call on an object gives that object's members the post-call
// versions the callee's contract describes. Nothing here changes the program:
// the classes erase to themselves, member for member.
//
// Each refused program in `negative/methods_*.cpp` is the other half of a pair
// with a function below that differs from it in one thing.
#include <cstdio>
#include <utility>

type Small = unsigned where (self < 10u);

// SPEC: CLASS-008, CLASS-009, CONTRACT-009
struct Counter {
    unsigned value;
    unsigned limit;

    // A const member function reads its object at entry and leaves it be.
    verified unsigned get() const
        ensures (result == value)
    {
        return value;
    }

    // A postcondition reads the object's normal-return post-state.
    verified void reset()
        ensures (value == 0u)
    {
        value = 0u;
    }

    // A precondition reads the entry state.
    verified void bump()
        expects (value < limit)
        ensures (value <= limit)
    {
        value = value + 1u;
    }

    // A member function calling another on its own object, written with and
    // without `this` (SPEC.md CLASS-011).
    verified unsigned twice() const
        ensures (result == value + value)
    {
        return get() + this->get();
    }

    // What a mutating call leaves is what its postcondition states: `reset`
    // promises `value == 0u`, and nothing about what `value` held before it
    // (`negative/methods_stale_member_after_call.cpp`).
    verified unsigned after_reset()
        expects (value == 3u)
        ensures (result == 0u)
    {
        reset();
        return value;
    }

    // A write to one member leaves a sibling's fact standing: distinct members
    // are distinct storage (SPEC.md CLASS-010).
    verified unsigned sibling()
        expects (value == 3u)
        ensures (result == 3u)
    {
        limit = 0u;
        return value;
    }

    // A reference parameter may designate a member of the object, so a write
    // through it invalidates the member's fact. Read before the write, the
    // value is the one the precondition speaks of
    // (`negative/methods_reference_alias_member.cpp`).
    verified unsigned read_before_alias(unsigned& other)
        expects (value == 3u)
        ensures (result == 3u)
    {
        const unsigned seen = value;
        other = 0u;
        return seen;
    }
};

// SPEC: CLASS-010
verified void zero(unsigned& x)
    ensures (x == 0u)
{
    x = 0u;
}

struct Pair {
    unsigned a;
    unsigned b;

    // A member passed by reference to a free function (SPEC.md CLASS-011).
    verified void zero_a()
        ensures (a == 0u)
    {
        zero(a);
    }

    verified unsigned read_b() const
        ensures (result == b)
    {
        return b;
    }

    // Writes `a` through a reference that may be `a` itself, and states `a`
    // after both writes.
    verified void set_both(unsigned& other, unsigned x)
        ensures (a == x)
    {
        other = x;
        a = x;
    }

    verified void put(unsigned& r) const
        ensures (r == 9u)
    {
        r = 9u;
    }
};

// A const member function writes nothing, so what the caller knew about the
// object survives the call (`negative/methods_mutable_member.cpp`).
verified unsigned const_call_keeps(unsigned x)
    ensures (result == x)
{
    Pair p{x, 7u};
    const unsigned seen = p.read_b();
    return p.a;
}

// A mutating call keeps exactly what its postcondition states
// (`negative/methods_mutating_call_forgets.cpp`).
verified unsigned mutating_call(unsigned x)
    ensures (result == 0u)
{
    Pair p{x, 7u};
    p.zero_a();
    return p.a;
}

// The object's member passed as the reference argument too: both are one
// storage, and share one post-call version (SPEC.md CLASS-011).
verified unsigned shared_argument(unsigned x)
    expects (x < 5u)
    ensures (result == x)
{
    Pair p{1u, 2u};
    p.set_both(p.a, x);
    return p.a;
}

// A const member function may still write through a reference argument that
// names a member of its own object (`negative/methods_const_call_through_alias.cpp`).
verified unsigned const_call_through_alias()
    ensures (result == 9u)
{
    Pair p{1u, 2u};
    p.put(p.a);
    return p.a;
}

// SPEC: CLASS-010, REFINE-060
struct Gauge {
    Small level;

    // A write to a refined member owes the member's predicate where the value
    // enters it (`negative/methods_refined_member_write.cpp`).
    verified void set(unsigned to)
        expects (to < 10u)
        ensures (level == to)
    {
        level = to;
    }

    // A refined member holds its predicate on entry; a refined result owes its
    // own on return.
    verified Small get() const
        ensures (result == level)
    {
        return level;
    }
};

// A free function building an object and calling its member functions.
verified unsigned gauge_round_trip(unsigned x)
    expects (x < 10u)
    ensures (result == x)
{
    Gauge g{0u};
    g.set(x);
    return g.get();
}

// SPEC: TERMINATION-004, TERMINATION-007
struct Tank {
    unsigned remaining;

    // A recursive member function, measured by a member it lowers
    // (`negative/methods_nondecreasing_recursion.cpp`).
    verified void drain()
        ensures (remaining == 0u)
        decreases (remaining)
    {
        if (remaining == 0u) {
            return;
        }
        remaining = remaining - 1u;
        drain();
    }

    // A loop over a member, with an invariant and a measure.
    verified void fill(unsigned amount)
        expects (amount < 100u)
        ensures (remaining == amount)
    {
        remaining = 0u;
        while (remaining < amount)
            invariant (remaining <= amount)
            decreases (amount - remaining)
        {
            remaining = remaining + 1u;
        }
    }
};

struct Inner {
    unsigned v;

    verified void set(unsigned x)
        ensures (v == x)
    {
        v = x;
    }
};

// SPEC: CLASS-008
struct Meter {
    unsigned level;
    Inner inner;
    unsigned items[3];

    // A member of a member is its own place, and a member function called on
    // it takes it as its implicit object.
    verified void nested(unsigned x)
        ensures (inner.v == x)
    {
        inner.set(x);
    }

    // A member array's elements are places, selected at a term like a local
    // array's.
    verified unsigned element(unsigned i)
        expects (i < 3u)
        ensures (result == result)
    {
        return items[i];
    }

    verified unsigned sum() const
        ensures (result == items[0] + items[1] + items[2])
    {
        return items[0] + items[1] + items[2];
    }

    // Overloads on the implicit object's constness are two callables, each
    // verified against its own contract; Clang selects which one a call runs.
    verified unsigned get() const
        ensures (result == level)
    {
        return level;
    }

    verified unsigned get()
        ensures (result == level)
    {
        return level;
    }

    // A reference qualifier keeps its C++ meaning.
    verified unsigned by_ref() const&
        ensures (result == level)
    {
        return level;
    }

    verified unsigned take() &&
        ensures (level == 0u)
    {
        const unsigned taken = level;
        level = 0u;
        return taken;
    }

    // A static member function has no implicit object.
    static verified unsigned add(unsigned a, unsigned b)
        expects (a < 100u && b < 100u)
        ensures (result == a + b)
    {
        return a + b;
    }

    // A contract on the declaration in the class; the definition below it
    // inherits it (SPEC.md CONTRACT-005).
    verified unsigned declared_here(unsigned x)
        expects (x < 5u)
        ensures (result == x + 1u);
};

unsigned Meter::declared_here(unsigned x) {
    return x + 1u;
}

verified unsigned static_use(unsigned a)
    expects (a < 50u)
    ensures (result == a + a)
{
    return Meter::add(a, a);
}

// SPEC: CLASS-010, CLASS-011
class Cursor {
  public:
    Cursor(unsigned p, unsigned l) : pos(p), len(l) {}

    verified unsigned position() const
        ensures (result == pos)
    {
        return pos;
    }

    verified unsigned length() const
        ensures (result == len)
    {
        return len;
    }

    // Another object of the class, passed by reference: its private members
    // read as the object's own do.
    verified unsigned distance(const Cursor& other) const
        expects (other.pos <= pos)
        ensures (result == pos - other.pos)
    {
        return pos - other.pos;
    }

    // `other` may be this very object. Read before `pos` is written, it holds
    // the value the precondition speaks of
    // (`negative/methods_reference_object_stale.cpp`).
    verified unsigned read_then_write(const Cursor& other)
        expects (other.pos == 4u)
        ensures (result == 4u)
    {
        const unsigned seen = other.pos;
        pos = 9u;
        return seen;
    }

    // The value before an unsafe block is known; after it, the object's
    // members are not (`negative/methods_unsafe_block.cpp`).
    verified unsigned before_unsafe()
        expects (pos == 1u)
        ensures (result == 1u)
    {
        const unsigned seen = pos;
        unsafe {
            pos = 7u;
        }
        return seen;
    }

  private:
    // Not modeled, never named by a verified body, and so never tracked.
    const char* text = nullptr;
    unsigned pos;
    unsigned len;
};

// Const member functions called on an object a parameter designates.
verified unsigned remaining(const Cursor& c)
    ensures (result == result)
{
    const unsigned length = c.length();
    const unsigned position = c.position();
    if (position <= length) {
        return length - position;
    }
    return 0u;
}

// And on one passed by value.
verified unsigned position_of(Cursor c)
    ensures (result == result)
{
    return c.position();
}

int main() {
    Counter counter{1u, 5u};
    counter.bump();
    const unsigned doubled = counter.twice();
    counter.reset();
    const unsigned after = counter.get();
    counter.value = 3u;
    const unsigned kept = counter.sibling();
    std::printf("%u %u %u %u\n", doubled, after, kept, counter.limit);

    std::printf("%u %u %u %u %u\n", const_call_keeps(3u), mutating_call(4u), shared_argument(2u),
                const_call_through_alias(), gauge_round_trip(6u));

    Tank tank{3u};
    tank.drain();
    tank.fill(5u);

    Meter meter{1u, {0u}, {1u, 2u, 3u}};
    meter.nested(7u);
    const Meter& view = meter;
    std::printf("%u %u %u %u %u %u %u %u %u\n", tank.remaining, view.get(), meter.get(), meter.inner.v,
                meter.element(1u), meter.sum(), view.by_ref(), static_use(2u), meter.declared_here(2u));
    const unsigned taken = std::move(meter).take();

    Cursor cursor{1u, 4u};
    const unsigned left = remaining(cursor);
    const unsigned at = position_of(cursor);
    const unsigned apart = cursor.distance(cursor);
    const unsigned before = cursor.before_unsafe();
    std::printf("%u %u %u %u %u %u %u\n", taken, meter.level, left, at, apart, before, cursor.position());
    return 0;
}
