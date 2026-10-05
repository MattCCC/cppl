// SPEC: LAW-008, ERASE-005, TEMPLATE-001
// An explicit instantiation names the specialization it instantiates in
// ordinary C++, where no Law can be found. Were `pick(0)` to find the Law, the
// specialization verified would be `g<sizeof(bool)>`, while the program
// instantiates and calls `g<sizeof(Seven)>`; the claim below holds only of the
// former, and is refused.
#include <cstdio>

struct Seven {
    char bytes[7];
};

Seven pick(long) {
    return Seven{};
}

law pick(int x)
    proves (x == x);

template <unsigned N>
verified unsigned g(unsigned y)
    ensures (result == 1u)
{
    return N;
}

template unsigned g<sizeof(pick(0))>(unsigned);

int main() {
    std::printf("%u\n", g<sizeof(pick(0))>(0u));
}
