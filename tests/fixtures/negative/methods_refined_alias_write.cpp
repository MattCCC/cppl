// SPEC: CLASS-010, REFINE-060, REFINEOBL-007
//
// `r` may designate `level`, so the value written through it may be what
// `level` holds afterwards, and it is charged `Small` where it is written.
// Called as `g.settle(g.level)` this leaves 50 in a `Small`. The accepted twin
// writes 5: `Gauge::settle` in `fixtures/verified_methods.cpp`, which owes
// nothing at its return.
type Small = unsigned where (self < 10u);

struct Gauge {
    Small level;

    verified void settle(unsigned& r)
        ensures (r == 50u)
    {
        r = 50u;
    }
};

int main() {
    return 0;
}
