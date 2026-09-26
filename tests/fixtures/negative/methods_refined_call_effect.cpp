// SPEC: CLASS-011, REFINE-060, REFINEOBL-007
//
// The call's effect on `level` is a value entering `Small`, and the caller is
// charged it where the call leaves it: here the callee's contract says 50. The
// accepted twin passes `level` to a `Small&` parameter the callee keeps valid:
// `Gauge::assign` in `fixtures/verified_methods.cpp`.
type Small = unsigned where (self < 10u);

verified void set_fifty(unsigned& x)
    ensures (x == 50u)
{
    x = 50u;
}

struct Gauge {
    Small level;

    verified void assign()
        ensures (level == 50u)
    {
        set_fifty(level);
    }
};

int main() {
    return 0;
}
