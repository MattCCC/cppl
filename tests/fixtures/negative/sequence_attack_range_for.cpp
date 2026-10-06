// SPEC: STDMODEL-015, STDMODEL-019, STMT-005
// A range-based `for` takes its range's beginning and end once, before the
// first iteration, so an iteration that may reallocate the range and goes on
// iterating reads storage that may no longer exist. Accepted twin: `sum_vector`
// in range_for.cpp, the same walk with nothing replacing the storage.
#include <cstddef>
#include <vector>

verified unsigned last_through_range()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned last = 0u;
    for (unsigned x : v) {
        last = x;
        v.push_back(x);
    }
    return last;
}

int main() {
    return 0;
}
