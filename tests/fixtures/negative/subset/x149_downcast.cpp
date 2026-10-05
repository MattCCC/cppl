// SPEC: CONSTRUCT-149
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// downcast, is refused.

struct Base {
    unsigned tag;
};
struct Derived : Base {
    unsigned extra;
};

verified unsigned probe(const Base& base)
    ensures (result == result)
{
    const Derived& derived = static_cast<const Derived&>(base);
    return derived.extra;
}

int main() {
    const Derived derived{{2u}, 3u};
    return probe(derived) == 3u ? 0 : 1;
}
