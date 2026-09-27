// SPEC: CONSTRUCT-113
// RFC 0022, the V1 verified subset: a verified body may use this construct, namespace declaration,
// and it is modeled. Its refused twin is negative/subset/x113_namespace_declaration.cpp.

namespace billing {
verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return x;
}
} // namespace billing

int main() {
    return billing::probe(2u) == 2u ? 0 : 1;
}
