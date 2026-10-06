// SPEC: ERASE-019, ERASE-005
// A declaration completes none of its parameter types, and the definition of
// zero stands after E::A. The probe of the declaration's clause is a
// definition taking Set<0> by value, so it would complete Set<0> before E::A is
// initialized, and which() would be verified to return 1 and run to print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>

verified unsigned zero(Set<0> s)
    ensures (result == 0u);

enum class E : unsigned { A = defined<0>() ? 1u : 0u };

verified unsigned which()
    ensures (result == 1u)
{
    return static_cast<unsigned>(E::A);
}

verified unsigned zero(Set<0> s)
    ensures (result == 0u)
{
    return 0u;
}

int main()
{
    std::printf("%u\n", which());
    return 0;
}
