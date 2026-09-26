// SPEC: CLASS-010, CLASS-011
//
// What a pointer designates may be what a reference designates, so the write
// through `r` may be a write to `p->v`: the value read before it is not the
// value after it. Called as `stale(&c, c.v)` this returns 5. The accepted twin
// writes nothing between: `read_through_pointer` in
// `fixtures/verified_methods.cpp`.
struct Cell {
    unsigned v;

    verified unsigned get() const
        ensures (result == v)
    {
        return v;
    }
};

verified unsigned stale(const Cell* p, unsigned& r)
    expects (readable(p))
    ensures (result == 1u)
{
    const unsigned before = p->get();
    if (before != 1u) {
        return 1u;
    }
    r = 5u;
    return p->get();
}

int main() {
    return 0;
}
