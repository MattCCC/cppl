// SPEC: ERASE-019, ERASE-010
// Only the predicate names Set<0>; the program run keeps the alias alone. The
// predicate's probe would instantiate Set<0> before E::A is initialized, and
// which() would be verified to return 1 and run to print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>

type Small = unsigned where (self < static_cast<unsigned>(Set<0>::Limit::value));

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
