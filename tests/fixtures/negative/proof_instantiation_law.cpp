// SPEC: ERASE-019, ERASE-005, LAW-008
// The Law's parameter is the only use of Set<0>. Projected for the analysis,
// it is a definition taking Set<0> by value, which instantiates Set<0> there
// and defines the friend adl(Tag<0>) before E::A is initialized. The program
// verified would then have E::A == 1 and prove which() returns 1, while the
// program run, which has no Law, prints 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>

law set_is_set(Set<0> s)
    proves (s.unused == s.unused);

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
