// SPEC: CONSTRUCT-135
// RFC 0022: the refused twin of subset/x135_concept_declaration.cpp (concept declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

#include <concepts>

template <typename T>
concept Unsigned = std::unsigned_integral<T>;

template <Unsigned T>
verified T probe(T x)
    ensures (result != x)
{
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
