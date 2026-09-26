// SPEC: CLASS-010, CLASS-011, REFINE-060, REFINE-062, TUBOUND-003
// Routes by which a refined place a function holds by reference gets a new
// value, each shown valid where it is: the effect of another unit's contract,
// which the caller is charged, on a member of the implicit object and on a
// reference parameter; and a loop whose every write is charged, where the
// value leaving the loop is the one at its head, which nothing established, so
// the return is charged it and the invariant shows it (TRUST.md TCB-OBJ-009).
// The refused twins are in tests/negative/cross_feature.sh.
#include "effects.hpp"

#include <cstdio>

struct Gauge {
    Small level;

    verified void assign_five()
        ensures (level == 5u)
    {
        set_five(level);
    }

    verified void settle(unsigned n)
        expects (level < 10u)
    {
        unsigned i = 0u;
        while (i < n)
            invariant (i <= n && level < 10u)
        {
            level = 5u;
            i = i + 1u;
        }
    }
};

verified unsigned through_reference(Small& s)
    ensures (result == 5u)
{
    set_five(s);
    return s;
}

verified unsigned observed()
    ensures (result == 5u)
{
    Small s = 3u;
    return through_reference(s);
}

int main() {
    std::printf("%u\n", observed());
    return 0;
}
