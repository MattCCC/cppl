// SPEC: CLASS-011, VERIFIED-031
//
// `put` is `const`, so it writes no member itself, but it writes through `r`,
// and `other` -- what `relay` passes as `r` -- may be `a`: `a` is a place the
// callee only reads that a write of the call may reach, and it takes a
// post-call version. Called as `p.relay(p.a)` this returns 9. The accepted twin
// reads `a` before the call: `Pair::relay_before` in
// `fixtures/verified_methods.cpp`.
struct Pair {
    unsigned a;
    unsigned b;

    verified void put(unsigned& r) const
        ensures (r == 9u)
    {
        r = 9u;
    }

    verified unsigned relay(unsigned& other)
        expects (a == 1u)
        ensures (result == 1u)
    {
        put(other);
        return a;
    }
};

int main() {
    return 0;
}
