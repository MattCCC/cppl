// SPEC: STDMODEL-018, TUBOUND-006
// Container contracts another unit proved, used here: each claim rests on the
// imported contract, so none is assumption-free, and one whose own body or
// callee's declaration uses a container names the model too.
#include "sequences.hpp"

#include <cstdio>

verified std::size_t through_listed()
    ensures (result == 3ul)
{
    return three_listed();
}

verified std::size_t through_copy()
    ensures (result == 3ul)
{
    std::vector<unsigned> v{1u, 2u};
    return grown_copy(v);
}

int main() {
    std::printf("%zu %zu\n", through_listed(), through_copy());
    return 0;
}
