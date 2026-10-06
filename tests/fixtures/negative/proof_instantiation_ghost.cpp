// SPEC: ERASE-019, ERASE-011
// Only the ghost declaration names Set<0>. Erased, it never runs, yet in the
// program verified it would instantiate Set<0> before E::A is initialized, and
// which() would be verified to return 1 and run to print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>

verified unsigned zero()
    ensures (result == 0u)
{
    ghost unsigned bound = static_cast<unsigned>(Set<0>::Limit::value);
    return 0u;
}

enum class E : unsigned { A = defined<0>() ? 1u : 0u };

verified unsigned which()
    ensures (result == 1u)
{
    return static_cast<unsigned>(E::A);
}

int main()
{
    std::printf("%u %u\n", which(), zero());
    return 0;
}
