// SPEC: CLASS-010, CLASS-011, VERIFIED-032
//
// `zero` writes through `r`, which may designate `a`: after the call `a` is
// whatever the common alias model cannot rule out, and the fact from the
// precondition no longer holds of it. Called as `p.forget(p.a)` this returns 0.
// The accepted twin reads `a` before the call: `Counter::read_before_alias` in
// `fixtures/verified_methods.cpp`.
verified void zero(unsigned& x)
    ensures (x == 0u)
{
    x = 0u;
}

struct Pair {
    unsigned a;
    unsigned b;

    verified unsigned forget(unsigned& r)
        expects (a == 3u)
        ensures (result == 3u)
    {
        zero(r);
        return a;
    }
};

int main() {
    return 0;
}
