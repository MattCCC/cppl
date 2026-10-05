// SPEC: CONSTRUCT-074
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// typeid, is refused.

#include <typeinfo>

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    const std::type_info& type = typeid(x);
    (void)type;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
