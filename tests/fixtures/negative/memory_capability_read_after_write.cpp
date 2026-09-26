// SPEC: VERIFIED-038
// A place a write formed is not thereby readable: reading through `p` needs
// `readable(p)` wherever the place was first reached, as a subscript's does.
// `writable` does not entail `readable`, and writing first does not earn it.
verified unsigned write_then_read(unsigned* p)
    expects (writable(p))
    ensures (result == 5u)
{
    *p = 5u;
    return *p;
}

int main() {
    return 0;
}
