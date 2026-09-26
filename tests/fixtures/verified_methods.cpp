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

    // A `const` call on this object writing through a reference that may be
    // `a`: `a` is read before it (`negative/methods_const_call_alias_param.cpp`).
    verified unsigned relay_before(unsigned& other)
        expects (a == 1u)
        ensures (result == 1u && other == 9u)
    {
        const unsigned seen = a;
        put(other);
        return seen;
    }

    // Writes through `other` first and through the member last: the member's
    // post-state is what the postcondition states, whatever `other` is.
    verified void overwrite(unsigned& other)
        ensures (a == 4u)
    {
        other = 3u;
        a = 4u;
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

// The same call writing storage apart from the object: no place of the object
// can be what the callee writes, so each keeps its version (SPEC.md CLASS-011).
verified unsigned const_call_apart(unsigned x)
    ensures (result == x)
{
    Pair p{x, 7u};
    unsigned elsewhere = 0u;
    p.put(elsewhere);
    return p.a;
}

// The member passed as the reference too: one storage and one post-call
// version, which the callee's postcondition about the member describes
// (SPEC.md CLASS-011, VERIFIED-031).
verified unsigned member_as_argument()
    ensures (result == 4u)
{
    Pair p{1u, 2u};
    p.overwrite(p.a);
    return p.a;
}

// SPEC: CLASS-011, REFINE-060
verified void set_small(Small& s, unsigned v)
    expects (v < 10u)
    ensures (s == v)
{
    s = v;
}

// SPEC: CLASS-010, REFINE-060, REFINE-062
struct Gauge {
    Small level;

    // A write through a reference that may be `level` is charged `Small` for
    // the value written, which may be what `level` holds afterwards. The
    // version it leaves in `level` holds `Small` either way, so the return is
    // charged nothing (`negative/methods_refined_alias_write.cpp`).
    verified void settle(unsigned& r)
        ensures (r == 5u)
    {
        r = 5u;
    }

    // A `const` member function writing through a reference that may be
    // `level` keeps `level` valid, as `settle` does.
    verified void settle_quietly(unsigned& r) const
        ensures (r == 5u)
    {
        r = 5u;
    }

    // Calling it with a reference that may be `level`: `level` is a place the
    // callee only reads that the call's write may reach, so it takes a
    // post-call version the callee's contract describes, `Small` included,
    // rather than an unknown one (SPEC.md CLASS-011).
    verified void relay(unsigned& other)
        ensures (other == 5u)
    {
        settle_quietly(other);
    }

    // What an unsafe block leaves in `level` is not known to hold `Small`, and
    // a write charged `Small` establishes it again
    // (`negative/methods_refined_unsafe_return.cpp`).
    verified void rescue()
        ensures (level == 3u)
    {
        unsafe {
            level = 50u;
        }
        level = 3u;
    }

    // A member passed to a `Small&` parameter: the call's effect on `level` is
    // charged `Small`, which the callee's contract establishes
    // (`negative/methods_refined_call_effect.cpp`).
    verified void assign(unsigned v)
        expects (v < 10u)
        ensures (level == v)
    {
        set_small(level, v);
    }

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

// Each route a value takes into `level`, one after another.
verified unsigned gauge_routes(unsigned x)
    expects (x < 10u)
    ensures (result == x)
{
    Gauge g{1u};
    unsigned outside = 0u;
    g.settle(outside);
    g.relay(outside);
    g.rescue();
    g.assign(x);
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

// SPEC: CLASS-009, CLASS-011
// An `&&` member function called on a named object moved from: the qualifier
// decides that the call may be made on it, and the call is made on the object's
// own places (`negative/methods_rvalue_stale.cpp`).
verified unsigned moved_take(unsigned x)
    ensures (result == 0u)
{
    Meter m{x, {0u}, {0u, 0u, 0u}};
    const unsigned taken = std::move(m).take();
    return m.level;
}

verified unsigned cast_take(unsigned x)
    ensures (result == 0u)
{
    Meter m{x, {0u}, {0u, 0u, 0u}};
    const unsigned taken = static_cast<Meter&&>(m).take();
    return m.level;
}

// SPEC: CLASS-011, VERIFIED-038
// The object a pointer parameter designates is a place, reached under the
// capability the contract states for the pointer: `readable` for what the
// callee may read, `writable` as well for what it may write
// (`negative/methods_pointer_receiver_capability.cpp`).
verified unsigned through_pointer(Pair* p)
    expects (readable(p) && writable(p))
    ensures (result == 0u)
{
    p->zero_a();
    return p->a;
}

verified unsigned read_through_pointer(const Pair* p)
    expects (readable(p))
    ensures (result == result)
{
    return (*p).read_b();
}

// SPEC: CLASS-012
// A static member function is a function: a pure one is a definition a
// contract may use.
struct Scale {
    unsigned factor;

    static pure unsigned twice(unsigned x) {
        return x + x;
    }

    verified unsigned doubled() const
        ensures (result == Scale::twice(factor))
    {
        return twice(factor);
    }
};

verified unsigned scaled(unsigned x)
    ensures (result == Scale::twice(x) + Scale::twice(x))
{
    return Scale::twice(x) + Scale::twice(x);
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

    Pair pair{5u, 6u};
    const unsigned zeroed = through_pointer(&pair);
    const Scale scale{21u};
    std::printf("%u %u %u %u %u %u %u %u %u %u\n", gauge_routes(8u), const_call_apart(4u), member_as_argument(),
                moved_take(9u), cast_take(9u), zeroed, read_through_pointer(&pair), scale.doubled(), scaled(3u),
                pair.a);
    return 0;
}
