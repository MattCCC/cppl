// SPEC: STDMODEL-023, TUBOUND-004
// An imported precondition is owed at the call exactly as a local one: the
// last index of a vector that may be empty is not asked for. Accepted twin:
// `at_last` in sequence_attacks_cross_tu/client.cpp, which states the vector is
// not empty.
#include <cstddef>

#include "storage.hpp"

verified unsigned unguarded_last(const std::vector<unsigned>& v)
    ensures (result == result)
{
    const std::size_t i = last_index(v);
    return v[i];
}

int main() {
    return 0;
}
