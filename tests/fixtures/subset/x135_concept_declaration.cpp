// SPEC: CONSTRUCT-135
// RFC 0022, the V1 verified subset: a verified body may use this construct, concept declaration,
// and it is modeled. Its refused twin is negative/subset/x135_concept_declaration.cpp.

#include <concepts>

template <typename T>
concept Unsigned = std::unsigned_integral<T>;

template <Unsigned T>
verified T probe(T x)
    ensures (result == x)
{
    return x;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
