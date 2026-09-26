// SPEC: STDMODEL-018, TUBOUND-006
// Container contracts another unit proved, used here: each claim rests on the
// imported contract, so none is assumption-free, and each names every model
// the other unit's proof used, whether in its declaration or only in its body,
// however many units away (TRUST.md TCB-LIB-010).
#include "sequences.hpp"
#include "sequences_middle.hpp"

#include <cstdio>

// Its callee's body used the std::vector model.
verified std::size_t through_listed()
    ensures (result == 3ul)
{
    return three_listed();
}

// The twin of `through_listed`: its callee's body used no model.
verified std::size_t through_counted()
    ensures (result == 3ul)
{
    return three_counted();
}

verified std::size_t through_copy()
    ensures (result == 3ul)
{
    std::vector<unsigned> v{1u, 2u};
    return grown_copy(v);
}

// Two units away from the body that used the model.
verified std::size_t through_middle()
    ensures (result == 3ul)
{
    return listed_in_the_middle();
}

int main() {
    std::printf("%zu %zu %zu %zu\n", through_listed(), through_counted(), through_copy(), through_middle());
    return 0;
}
