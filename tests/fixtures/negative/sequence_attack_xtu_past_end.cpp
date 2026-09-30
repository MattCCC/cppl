// SPEC: STDMODEL-012, TUBOUND-004
// An imported result bounded by `<= size()` may be `size()`, one past the end.
// Accepted twin: `at_last` in sequence_attacks_cross_tu/client.cpp, whose index
// is bounded by `< size()`.
#include <cstddef>

#include "storage.hpp"

verified unsigned past_the_end(const std::vector<unsigned>& v)
    ensures (result == result)
{
    const std::size_t i = end_index(v);
    return v[i];
}

int main() {
    return 0;
}
