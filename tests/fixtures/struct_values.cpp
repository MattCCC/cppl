// SPEC: STORAGE-007, STORAGE-008, CLASS-008, CLASS-011, CONSTRUCT-142, CONSTRUCT-143, CONSTRUCT-144
// SPEC: CONSTRUCT-145
//
// Whole struct values in verified bodies: initialized from, passed, returned,
// assigned and handed by reference (TRUST.md TCB-AGGREGATE-001). A struct a body
// tracks is one place per scalar leaf; a value of it that flows anywhere is
// assembled from those places, and a value that flows into it is taken member by
// member. Each function's contract is proven, and main prints what was proven.
#include <array>
#include <cstdio>

type Percent = unsigned where (self <= 100u);

struct Config {
    unsigned limit;
    unsigned step;
};

struct Outer {
    Config inner;
    unsigned tag;
};

struct Buffer {
    unsigned items[3];
    unsigned count;
};

struct Table {
    std::array<unsigned, 2> cells;
    unsigned rows;
};

struct Gauge {
    Percent level;
    unsigned id;
};

struct Box {
    unsigned value;

    verified unsigned get() const
        ensures (result == value)
    {
        return value;
    }
};

verified unsigned peek(const Config& c)
    ensures (result == c.limit)
{
    return c.limit;
}

struct Holder {
    Config cfg;
    unsigned uses;

    // A member of the implicit object, and the implicit object itself, read whole.
    verified Config config() const
        ensures (result.limit == cfg.limit && result.step == cfg.step)
    {
        return cfg;
    }

    verified Holder copy() const
        ensures (result.uses == uses && result.cfg.limit == cfg.limit)
    {
        return *this;
    }

    // A member of the implicit object handed on by reference.
    verified unsigned limit_of() const
        ensures (result == cfg.limit)
    {
        return peek(cfg);
    }
};

verified unsigned by_value(Config c)
    ensures (result == c.step)
{
    return c.step;
}

verified Config make(unsigned l)
    ensures (result.limit == l && result.step == 1u)
{
    Config c{l, 1u};
    return c;
}

verified Config pass_through(const Config& c)
    ensures (result.limit == c.limit && result.step == c.step)
{
    return c;
}

// A local copied from a parameter the caller passes by reference.
verified unsigned copy_param(const Config& c)
    ensures (result == c.limit)
{
    Config d = c;
    return d.limit;
}

// A local handed on by reference, and by value.
verified unsigned only_read()
    ensures (result == 100u)
{
    Config c{100u, 5u};
    return peek(c);
}

verified unsigned pass_by_value()
    ensures (result == 5u)
{
    Config c{100u, 5u};
    return by_value(c);
}

// A local initialized from a call's result.
verified unsigned from_call()
    ensures (result == 7u)
{
    Config d = make(7u);
    return d.limit;
}

// A struct handed by reference beside a scalar of another local the callee
// writes: no write of the call reaches the struct, so it keeps its value.
verified void advance(const Config& c, unsigned& s)
    expects (s < 1000u && c.step < 1000u)
    ensures (true)
{
    s = s + c.step;
}

verified unsigned run()
    ensures (result == 100u)
{
    Config c{100u, 5u};
    unsigned s = 10u;
    advance(c, s);
    return c.limit;
}

// A struct handed by reference with one of its own members by mutable
// reference: the member the callee writes is not kept, its sibling is.
verified unsigned sibling_kept()
    ensures (result == 100u)
{
    Config c{100u, 5u};
    advance(c, c.step);
    return c.limit;
}

// A struct a callee may write, through a mutable reference: afterwards each
// member is what the callee's contract states of the struct's post-state.
verified unsigned look(Config& c)
    ensures (result == c.limit)
{
    return c.limit;
}

verified unsigned after_look()
    ensures (result == 1u)
{
    Config c{11u, 2u};
    unsigned seen = look(c);
    return seen == c.limit ? 1u : 0u;
}

// Nested structs: a member that is itself a struct, copied and handed on.
verified unsigned nested_copy(const Outer& o)
    ensures (result == o.inner.step)
{
    Config d = o.inner;
    return d.step;
}

verified Outer wrap(unsigned l, unsigned t)
    ensures (result.inner.limit == l && result.tag == t)
{
    Outer o{{l, 2u}, t};
    return o;
}

verified unsigned nested_local()
    ensures (result == 9u)
{
    Outer o = wrap(9u, 4u);
    return peek(o.inner);
}

// Arrays as members, built in and std::array.
verified unsigned buffer_sum(Buffer b)
    expects (b.items[0] < 100u && b.items[2] < 100u)
    ensures (result == b.items[0] + b.items[2])
{
    return b.items[0] + b.items[2];
}

verified unsigned buffer_flow()
    ensures (result == 12u)
{
    Buffer b{{5u, 6u, 7u}, 3u};
    Buffer c = b;
    return buffer_sum(c);
}

verified unsigned table_copy(const Table& t)
    ensures (result == t.cells[1])
{
    Table u = t;
    return u.cells[1];
}

// An array of structs, an element handed on.
verified unsigned element_passed()
    ensures (result == 3u)
{
    Config pair[2]{{1u, 2u}, {3u, 4u}};
    return peek(pair[1]);
}

// A refined member: established where the struct is built, kept through a copy,
// owed where the struct is passed, and stated of a result.
verified unsigned level_of(Gauge g)
    ensures (result <= 100u)
{
    return g.level;
}

verified Gauge make_gauge(Percent p)
    ensures (result.level == p)
{
    Gauge g{p, 7u};
    return g;
}

verified unsigned gauge_flow()
    ensures (result <= 100u)
{
    Gauge g = make_gauge(42u);
    Gauge h = g;
    return level_of(h);
}

verified unsigned id_of(Gauge& g)
    ensures (result == g.id)
{
    return g.id;
}

verified unsigned refined_after_call()
    ensures (result <= 100u)
{
    Gauge g{50u, 1u};
    unsigned i = id_of(g);
    return level_of(g) + 0u * i;
}

// A struct returned and passed on.
verified unsigned chained()
    ensures (result == 6u)
{
    return peek(pass_through(make(6u)));
}

// A const member function on a struct obtained by value.
verified Box make_box(unsigned v)
    ensures (result.value == v)
{
    Box b{v};
    return b;
}

verified unsigned member_on_value()
    ensures (result == 3u)
{
    Box b = make_box(3u);
    return b.get();
}

// Assigned whole: from another object, from a call, and to itself.
verified unsigned assigned(const Config& c)
    ensures (result == c.limit)
{
    Config d{0u, 0u};
    d = c;
    return d.limit;
}

verified unsigned assigned_from_call()
    ensures (result == 8u)
{
    Config d{0u, 0u};
    d = make(8u);
    return d.limit;
}

verified unsigned self_assigned()
    ensures (result == 9u)
{
    Config c{9u, 2u};
    c = c;
    return c.limit;
}

// A by-value parameter written, then returned whole: the member written is the
// new value, the other the one passed.
verified Config bump(Config c)
    ensures (result.limit == 5u && result.step == c.step)
{
    c.limit = 5u;
    return c;
}

int main() {
    const Config five = make(5u);
    const Outer outer = wrap(2u, 3u);
    const Table table{{4u, 6u}, 1u};
    const Holder holder{{7u, 2u}, 1u};
    std::printf("%u %u %u %u %u %u %u %u\n", copy_param(five), only_read(), pass_by_value(), from_call(), run(),
                sibling_kept(), after_look(), nested_copy(outer));
    std::printf("%u %u %u %u %u %u %u %u\n", nested_local(), buffer_flow(), table_copy(table), element_passed(),
                gauge_flow(), refined_after_call(), chained(), member_on_value());
    std::printf("%u %u %u %u %u %u %u\n", assigned(five), assigned_from_call(), self_assigned(), bump(five).limit,
                holder.config().limit, holder.copy().uses, holder.limit_of());
    return 0;
}
