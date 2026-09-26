// SPEC: CLASS-011, CLASS-015
//
// Receivers this implementation forms no sound place for, each refused where
// the call is written: an object a parameter designates by reference, which is
// read here as one value and never written member by member; an element
// selected at a term of an array of class type, whose members have no place;
// and a temporary, which is a new object rather than one the caller holds.
struct Cell {
    unsigned v;

    verified void set(unsigned x)
        ensures (v == x)
    {
        v = x;
    }

    verified unsigned get() const
        ensures (result == v)
    {
        return v;
    }
};

verified unsigned through_reference(Cell& c)
    ensures (result == 5u)
{
    c.set(5u);
    return c.get();
}

verified unsigned at_index(unsigned i)
    expects (i < 3u)
    ensures (result == 7u)
{
    Cell cells[3] = {{7u}, {7u}, {7u}};
    return cells[i].get();
}

verified unsigned temporary()
    ensures (result == 7u)
{
    return Cell{7u}.get();
}

int main() {
    return 0;
}
