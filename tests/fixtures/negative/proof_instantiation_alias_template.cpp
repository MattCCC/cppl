// SPEC: ERASE-019, LAW-008
// An alias template forms a type, and forming this one evaluates
// sizeof(Set<N>). The Law's parameter type is `unsigned`, but naming Word<0>
// completes Set<0> before E::A is initialized, and which() would be verified
// to return 1 and run to print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>
#include <type_traits>

template <int N>
using Word = std::conditional_t<sizeof(Set<N>) != 0u, unsigned, int>;

law reflexive(Word<0> x)
    proves (x == x);

enum class E : unsigned { A = defined<0>() ? 1u : 0u };

verified unsigned which()
    ensures (result == 1u)
{
    return static_cast<unsigned>(E::A);
}

int main()
{
    std::printf("%u\n", which());
    return 0;
}
