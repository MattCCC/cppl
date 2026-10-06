// SPEC: ERASE-019, LOOP-001
// Only the loop invariant names Set<0>. It would instantiate Set<0> in the
// program verified before E::A is initialized, and which() would be verified to
// return 1 and run to print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>

verified unsigned count(unsigned n)
    expects (n < 10u)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n && n < static_cast<unsigned>(Set<0>::Limit::value))
        decreases (n - i)
    {
        i = i + 1u;
    }
    return i;
}

enum class E : unsigned { A = defined<0>() ? 1u : 0u };

verified unsigned which()
    ensures (result == 1u)
{
    return static_cast<unsigned>(E::A);
}

int main()
{
    std::printf("%u %u\n", which(), count(3u));
    return 0;
}
