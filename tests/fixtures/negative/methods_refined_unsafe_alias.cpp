// SPEC: CLASS-010, REFINE-060, REFINE-061
//
// The write through `r` is charged `Small` for 5, and if `r` is `level` that is
// what `level` holds afterwards. If it is not, `level` holds what the unsafe
// block left there, which nothing charged. A charged aliasing write keeps a
// place valid only where it was valid before, so here the return is charged
// `Small` for `level`, and cannot show it. The accepted twin has no unsafe
// block: `Gauge::settle` in `fixtures/verified_methods.cpp`.
type Small = unsigned where (self < 10u);

struct Gauge {
    Small level;

    verified void scramble_then_settle(unsigned& r)
        ensures (r == 5u)
    {
        unsafe {
            level = 50u;
        }
        r = 5u;
    }
};

int main() {
    return 0;
}
