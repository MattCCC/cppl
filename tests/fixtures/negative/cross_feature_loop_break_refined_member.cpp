// SPEC: CLASS-010, UNSAFE-005, LOOP-001, REFINE-061
// `Gauge::settle` of fixtures/cross_feature/effects_client.cpp with an unsafe
// write before a `break` and the charged write after it. The loop's invariant
// states the refinement at the head, but the `break` leaves with the value the
// block wrote, which neither the head nor a charge established: the return is
// charged `Small` for it and cannot show it (TRUST.md TCB-OBJ-009).
type Small = unsigned where (self < 10u);

struct Gauge {
    Small level;

    verified void settle(unsigned n)
        expects (level < 10u)
    {
        unsigned i = 0u;
        while (i < n)
            invariant (i <= n && level < 10u)
        {
            unsafe {
                level = 50u;
            }
            if (i == 1u) {
                break;
            }
            level = 5u;
            i = i + 1u;
        }
    }
};
