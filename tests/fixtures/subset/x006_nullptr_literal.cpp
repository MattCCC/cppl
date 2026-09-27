// SPEC: CONSTRUCT-006
// RFC 0022, the V1 verified subset: a verified body may use this construct, nullptr literal,
// and it is modeled. Its refused twin is negative/subset/x006_nullptr_literal.cpp.

verified unsigned probe(const unsigned* p)
    ensures (result <= 1u)
{
    if (p == nullptr) {
        return 0u;
    }
    return 1u;
}

int main() {
    return probe(nullptr) == 0u ? 0 : 1;
}
