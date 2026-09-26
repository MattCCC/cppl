// Verified member functions erase to the member functions as written (SPEC.md
// ERASE-004, ABI-001, CLASS-013). The contracts leave, and nothing takes their
// place: no member, no base, no virtual table, no changed signature, no check.
// `methods.reference.cpp` is this program erased by hand.
#include <cstddef>
#include <cstdio>
#include <utility>

type Small = unsigned where (self < 10u);

struct Inner {
    unsigned v;

    verified void set(unsigned x)
        ensures (v == x)
    {
        v = x;
    }
};

class Meter {
  public:
    Inner inner;
    unsigned items[3];

    Meter(unsigned level, unsigned first) : inner{first}, items{1u, 2u, 3u}, level_(level) {}

    verified unsigned get() const
        ensures (result == level_)
    {
        return level_;
    }

    verified unsigned get()
        ensures (result == level_)
    {
        return level_;
    }

    verified void raise(unsigned to)
        expects (to < 100u)
        ensures (level_ == to)
    {
        level_ = to;
    }

    verified void nested(unsigned x)
        ensures (inner.v == x)
    {
        inner.set(x);
    }

    verified unsigned sum(unsigned i)
        expects (i < 3u)
        ensures (result == result)
    {
        unsigned total = 0u;
        unsigned index = 0u;
        while (index < 3u)
            invariant (index <= 3u)
            decreases (3u - index)
        {
            total = total + items[index];
            index = index + 1u;
        }
        return total + items[i];
    }

    verified unsigned take() &&
        ensures (level_ == 0u)
    {
        const unsigned taken = level_;
        level_ = 0u;
        return taken;
    }

    static verified unsigned add(unsigned a, unsigned b)
        expects (a < 100u && b < 100u)
        ensures (result == a + b)
    {
        return a + b;
    }

    verified unsigned declared_here(unsigned x)
        expects (x < 5u)
        ensures (result == x + 1u);

  private:
    unsigned level_;
};

unsigned Meter::declared_here(unsigned x) {
    return x + 1u;
}

struct Gauge {
    Small level;

    verified void set(unsigned to)
        expects (to < 10u)
        ensures (level == to)
    {
        level = to;
    }
};

verified unsigned gauge(unsigned x)
    expects (x < 10u)
    ensures (result == x)
{
    Gauge g{0u};
    g.set(x);
    return g.level;
}

static_assert(sizeof(Meter) == 5 * sizeof(unsigned));
static_assert(alignof(Meter) == alignof(unsigned));
static_assert(offsetof(Inner, v) == 0);
static_assert(sizeof(Gauge) == sizeof(unsigned));

int main() {
    Meter meter{4u, 0u};
    meter.raise(6u);
    meter.nested(7u);
    const Meter& view = meter;
    std::printf("%u %u %u %u %u %u %u\n", view.get(), meter.get(), meter.inner.v, meter.sum(1u), Meter::add(2u, 3u),
                meter.declared_here(1u), gauge(9u));
    const unsigned taken = std::move(meter).take();
    std::printf("%u %u\n", taken, meter.get());
    return 0;
}
