// SPEC: CLASS-010, UNSAFE-005, LOOP-005, REFINE-061
// `Gauge::settle` of fixtures/cross_feature/effects_client.cpp with its write
// made in an unsafe block. The value leaving the loop is the one at its head,
// which the block may have written, and nothing charged it: the return is
// charged `Small` for `level` and cannot show it, since the loop may have left
// 50 there (TRUST.md TCB-OBJ-009).
type Small = unsigned where (self < 10u);

struct Gauge {
    Small level;

    verified void settle(unsigned n)
        expects (level < 10u)
    {
        unsigned i = 0u;
        while (i < n)
            invariant (i <= n)
        {
            unsafe {
                level = 50u;
            }
            i = i + 1u;
        }
    }
};
