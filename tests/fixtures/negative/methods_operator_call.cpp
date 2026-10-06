// SPEC: CLASS-015
// A verified operator function is verified, but a call through its operator
// from a verified body is not modeled, and is refused by name rather than read
// as some other call.
struct Adder {
    unsigned base;
    verified unsigned operator()(unsigned k) const
        expects (base < 100u && k < 100u)
        ensures (result == base + k)
    {
        return base + k;
    }
};

verified unsigned call_it()
    ensures (result == 4u)
{
    Adder add{1u};
    return add(3u);
}

int main() {
    return call_it() == 4u ? 0 : 1;
}
