// SPEC: ERASE-019, ERASE-005
// Only the argument a proof's step instantiates a proof at names Set<0>. It is
// resolved in the program verified, where it would instantiate Set<0> before
// E::A is initialized, and which() would be verified to return 1 and run to
// print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>

law reflexive(unsigned x)
    proves (x == x);

proof reflexive_holds(unsigned x)
    proves (reflexive(x))
{
    refl;
}

law shifted(unsigned x)
    proves (x + 10u == x + 10u);

proof shifted_holds(unsigned x)
    proves (shifted(x))
{
    exact reflexive_holds(x + static_cast<unsigned>(Set<0>::Limit::value));
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
