// SPEC: STDMODEL-019
// A range-based `for` over a sequence walks it through iterators, which are not
// modeled. Accepted twin: `last_by_index` in sequence_attacks.cpp, the same walk
// by index.
#include <cstddef>
#include <vector>

verified unsigned last_through_range()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned last = 0u;
    for (unsigned x : v) {
        last = x;
    }
    return last;
}

int main() {
    return 0;
}
