// The accepted halves of two matched pairs whose refused halves are
// `negative/member_numbering_gap.cpp` and `negative/unsafe_member_write.cpp`.
//
// A parameter whose type has a member this implementation does not model, or a
// member it cannot state, is not tracked as places. Its modeled members are
// still read by name, as projections of the value the parameter arrived with,
// and an unsafe block that leaves it as it was does not disturb that.
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

int main() {
    const Gap gap{0.0f, 1u, 2u};
    const Hidden hidden{3u, 4u};
    std::printf("%u %u\n", read_b(gap), keep(hidden));
    return 0;
}
