// SPEC: CLASS-011, VERIFIED-038, VERIFIED-043
//
// A member function called on what a pointer designates reaches its places
// only under the capability the contract states for the pointer: `readable`
// for any call, and `writable` as well for one that may write. Neither follows
// from the pointer being non-null. The accepted twins state both:
// `through_pointer` and `read_through_pointer` in
// `fixtures/verified_methods.cpp`.
struct Cell {
    unsigned v;

    verified unsigned get() const
        ensures (result == v)
    {
        return v;
    }

    verified void set(unsigned x)
        ensures (v == x)
    {
        v = x;
    }
};

verified unsigned read_unstated(Cell* p)
    expects (p != nullptr)
    ensures (result == result)
{
    return p->get();
}

verified unsigned write_read_only(Cell* p)
    expects (readable(p))
    ensures (result == 4u)
{
    p->set(4u);
    return p->get();
}

int main() {
    return 0;
}
