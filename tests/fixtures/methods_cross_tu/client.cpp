// Calls member functions whose bodies are in another translation unit. Each
// contract is used only as `counter.cpp` recorded proving it, with the object's
// places passed as the implicit object's arguments (SPEC.md CLASS-011,
// TUBOUND-003). The refused halves are `negative/methods_cross_tu_*.cpp`.
#include "counter.hpp"

#include <cstdio>

// A mutating call, then a const one: what is known after `reset` is what its
// postcondition states.
verified unsigned read_after_reset(unsigned start)
    ensures (result == 0u)
{
    Counter counter{start, 10u};
    counter.reset();
    return counter.get();
}

// The callee's precondition is owed at the object's places.
verified unsigned room(unsigned start)
    expects (start <= 10u)
    ensures (result == 10u - start)
{
    const Counter counter{start, 10u};
    return counter.headroom();
}

int main() {
    std::printf("%u %u\n", read_after_reset(4u), room(3u));
    return 0;
}
