// SPEC: STDMODEL-012, ARITH-003
// The last element of a vector that may be empty: `size() - 1` wraps to
// SIZE_MAX there. Accepted twin: `last_of_nonempty` in sequence_attacks.cpp.
#include <cstddef>
#include <vector>

verified unsigned last_of(const std::vector<unsigned>& v)
    ensures (result == result)
{
    return v[v.size() - 1ul];
}

int main() {
    return 0;
}
