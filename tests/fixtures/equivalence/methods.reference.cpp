// `methods.cpp`, erased by hand: every contract and the `verified` specifier
// removed, the refinement lowered to its base type, and nothing else changed.
#include <cstddef>
#include <cstdio>
#include <utility>

using Small = unsigned;

struct Inner {
    unsigned v;

    void set(unsigned x) {
        v = x;
    }
};

class Meter {
  public:
    Inner inner;
    unsigned items[3];

    Meter(unsigned level, unsigned first) : inner{first}, items{1u, 2u, 3u}, level_(level) {}

    unsigned get() const {
        return level_;
    }

    unsigned get() {
        return level_;
    }

    void raise(unsigned to) {
        level_ = to;
    }

    void nested(unsigned x) {
        inner.set(x);
    }

    unsigned sum(unsigned i) {
        unsigned total = 0u;
        unsigned index = 0u;
        while (index < 3u) {
            total = total + items[index];
            index = index + 1u;
        }
        return total + items[i];
    }

    unsigned take() && {
        const unsigned taken = level_;
        level_ = 0u;
        return taken;
    }

    static unsigned add(unsigned a, unsigned b) {
        return a + b;
    }

    unsigned declared_here(unsigned x);

  private:
    unsigned level_;
};

unsigned Meter::declared_here(unsigned x) {
    return x + 1u;
}

struct Gauge {
    Small level;

    void set(unsigned to) {
        level = to;
    }
};

unsigned gauge(unsigned x) {
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
