// SPEC: ERASE-019, CASE-017
// The optional is taken by reference, so the program run never completes
// Set<0>. Binding the payload of the split does, in the program verified, before
// E::A is initialized, and which() would be verified to return 1 and run to
// print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>
#include <optional>

verified unsigned split(const std::optional<Set<0>>& o)
    ensures (result == 0u)
{
    cases o {
        some(payload) => {
        }

        none => {
        }
    }
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
