// SPEC: CLASS-010, UNSAFE-005, REFINE-061
// A reference parameter's twin of fixtures/cross_feature/effects_client.cpp's
// `Gauge::settle`: a `return` inside the loop, after an unsafe write and before
// the charged one, leaves with the value the block wrote. The return there is
// charged `Small` for `s` and cannot show it (TRUST.md TCB-OBJ-009).
type Small = unsigned where (self < 10u);

verified void settle(Small& s, unsigned n)
    expects (s < 10u)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n && s < 10u)
    {
        unsafe {
            s = 50u;
        }
        if (i == 1u) {
            return;
        }
        s = 5u;
        i = i + 1u;
    }
}
