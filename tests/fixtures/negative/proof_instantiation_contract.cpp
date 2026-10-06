// SPEC: ERASE-019, ERASE-005
// Only the postcondition reads the Set<0> it is given by reference; the body
// and the program run never complete Set<0>. The clause's probe would, before
// E::A is initialized, and which() would be verified to return 1 and run to
// print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>

verified unsigned zero(const Set<0>& s)
    ensures (s.unused == s.unused && result == 0u)
{
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
    std::printf("%u\n", which());
    return 0;
}
