// SPEC: STDMODEL-025, TUBOUND-004
// A contract imported from another unit that takes a vector by mutable
// reference may reallocate it, whatever its postcondition says, so a span formed
// before the call is stale after it. Accepted twin: `read_then_append` in
// sequence_attacks_cross_tu/client.cpp.
#include <cstddef>
#include <span>

#include "storage.hpp"

verified unsigned stale_across_units()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u};
    std::span<const unsigned> s(v);
    append_one(v);
    return s[0];
}

int main() {
    return 0;
}
