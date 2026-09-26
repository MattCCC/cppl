// SPEC: CONTRACT-010, CLASS-011
// A const member function still writes through its reference parameters, and
// the one passed here is a member of the object it is called on: `p.a` is 9
// after the call, not the 1 it held before. The accepted half claims the 9:
// `const_call_through_alias` in `fixtures/verified_methods.cpp`.
struct Pair {
    unsigned a;
    unsigned b;

    verified void put(unsigned& r) const
        ensures (r == 9u)
    {
        r = 9u;
    }
};

verified unsigned const_call_through_alias()
    ensures (result == 1u)
{
    Pair p{1u, 2u};
    p.put(p.a);
    return p.a;
}

int main() {
    return static_cast<int>(const_call_through_alias());
}
