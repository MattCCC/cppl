// SPEC: CONSTRUCT-044
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// three-way comparison, is refused.

#include <compare>

verified unsigned probe(unsigned a, unsigned b)
    ensures (result <= 1u)
{
    if ((a <=> b) < 0) {
        return 1u;
    }
    return 0u;
}

int main() { return probe(1u, 2u) == 1u ? 0 : 1; }
