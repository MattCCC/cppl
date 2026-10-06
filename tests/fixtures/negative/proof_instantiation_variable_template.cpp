// SPEC: ERASE-019, LAW-008
// The Law's parameter type is `unsigned`, chosen by a variable template's
// specialization, whose initializer completes Set<0> before E::A is
// initialized, and which() would be verified to return 1 and run to print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>
#include <type_traits>

template <int N>
constexpr bool small = sizeof(Set<N>) < 100u;

law reflexive(std::conditional_t<small<0>, unsigned, int> x)
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
