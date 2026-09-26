// The accepted halves of three matched pairs whose refused halves are
// `negative/member_numbering_gap.cpp`, `negative/unsafe_member_write.cpp` and
// `negative/reference_aggregate_stale.cpp`.
//
// A parameter whose type has a member this implementation does not model, or a
// member it cannot state, is not tracked as places. Its modeled members are
// still read by name, as projections of the value the parameter arrived with,
// and an unsafe block that leaves it as it was does not disturb that. An object
// passed by reference is one place, read as the value it holds where the read
// stands.
#include <cstdio>

struct Gap {
    float f;
    unsigned a;
    unsigned b;
};

verified unsigned read_b(Gap s)
    ensures (result == s.b)
{
    unsafe {}
    return s.b;
}

struct Hidden {
    unsigned x;
    Hidden(unsigned a, unsigned b) : x(a), y(b) {}

  private:
    unsigned y;
};

unsafe unsigned observe(unsigned value) {
    return value;
}

verified unsigned keep(Hidden s)
    ensures (result == s.x)
{
    unsafe {
        observe(s.x);
    }
    return s.x;
}

struct Pair {
    unsigned a;
    unsigned b;
};

// Read before the write through `r`: the value `other.a` arrived with.
verified unsigned read_first(unsigned& r, const Pair& other)
    expects (other.a == 1u)
    ensures (result == 1u)
{
    const unsigned seen = other.a;
    r = 5u;
    return seen;
}

int main() {
    const Gap gap{0.0f, 1u, 2u};
    const Hidden hidden{3u, 4u};
    Pair pair{1u, 2u};
    const unsigned first = read_first(pair.a, pair);
    std::printf("%u %u %u %u\n", read_b(gap), keep(hidden), first, pair.a);
    return 0;
}
