// SPEC: STDMODEL-020
// `std::array<Positive, 2>` is `std::array<unsigned, 2>`: the refinement is a
// template argument and does not travel with the specialization, and no
// content invariant is modeled for an array. Read as the base type it would
// accept the write below, which its declaration says it refuses, so it is
// refused instead. Accepted twins: a built-in array `Positive a[2]`, whose
// elements are refined storage (`negative/refinement_types.sh`), and
// `vector_refined` in `fixtures/containers.cpp`.
#include <array>

type Positive = unsigned where (self > 0u);

verified unsigned write_zero()
    ensures (true)
{
    std::array<Positive, 2> a{1u, 2u};
    a[0] = 0u;
    return a[1];
}

int main() {
    return 0;
}
