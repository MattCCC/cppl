// SPEC: CLASS-011, TUBOUND-003
//
// `read_after_reset` of `fixtures/methods_cross_tu/client.cpp`, claiming the
// member still holds what it held before the mutating call. The recorded
// contract of `reset` states only `value == 0u`, so this claim, false at run
// time, must not be proven through it.
#include "counter.hpp"

verified unsigned stale(unsigned start)
    ensures (result == start)
{
    Counter counter{start, 10u};
    counter.reset();
    return counter.get();
}

int main() {
    return static_cast<int>(stale(4u));
}
