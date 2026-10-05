// SPEC: CONSTRUCT-065
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// dynamic_cast, is refused.

struct Base {
    virtual ~Base() = default;
};
struct Derived : Base {};

verified unsigned probe(const Base& base)
    ensures (result <= 1u)
{
    if (dynamic_cast<const Derived*>(&base) != nullptr) {
        return 1u;
    }
    return 0u;
}

int main() {
    const Derived derived;
    return probe(derived) == 1u ? 0 : 1;
}
