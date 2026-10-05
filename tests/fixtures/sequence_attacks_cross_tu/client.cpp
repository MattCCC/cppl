// The accepted twins of the cross-unit attacks in `tests/negative/
// sequence_attacks.sh`: each uses an imported contract only as far as it states.
#include "storage.hpp"

#include <cstddef>
#include <cstdio>
#include <span>

// SPEC: STDMODEL-025, TUBOUND-004
// The span is read before the imported call that may reallocate its storage.
// Refused twin: `negative/sequence_attack_xtu_stale.cpp`.
verified unsigned read_then_append()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u};
    std::span<const unsigned> s(v);
    const unsigned first = s[0];
    append_one(v);
    return first;
}

// SPEC: STDMODEL-012, STDMODEL-023
// The imported postcondition bounds the index strictly, and the imported
// precondition is owed here. Refused twins: `negative/sequence_attack_xtu_past_end.cpp`
// indexes with a bound, `negative/sequence_attack_xtu_unguarded_last.cpp` owes
// the precondition of a vector that may be empty.
verified unsigned at_last(const std::vector<unsigned>& v)
    expects (0ul < v.size())
    ensures (result == result)
{
    const std::size_t i = last_index(v);
    return v[i];
}

int main() {
    const std::vector<unsigned> v{4u, 5u};
    std::printf("%u %u\n", read_then_append(), at_last(v));
    return 0;
}
