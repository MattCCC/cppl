// SPEC: CONSTRUCT-148
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// base class conversion, is refused.

struct Base {
    unsigned tag;
};
struct Derived : Base {
    unsigned extra;
};

verified unsigned probe(const Derived& derived)
    ensures (result == result)
{
    const Base& base = derived;
    return base.tag;
}

int main() {
    const Derived derived{{2u}, 3u};
    return probe(derived) == 2u ? 0 : 1;
}
