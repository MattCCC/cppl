// SPEC: VERIFIED-038
// A place a read formed is not thereby writable: writing through `p` needs
// `writable(p)` wherever the place was first reached. `p` may point to a
// const object, which only `readable` allows.
verified unsigned read_then_write(unsigned* p)
    expects (readable(p))
    ensures (result == result)
{
    unsigned first = *p;
    *p = 5u;
    return first;
}

int main() {
    return 0;
}
