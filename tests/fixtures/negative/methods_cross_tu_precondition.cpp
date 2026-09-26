// SPEC: CLASS-011, TUBOUND-003
//
// `room` of `fixtures/methods_cross_tu/client.cpp` without its precondition:
// the recorded contract of `headroom` is used only where its precondition is
// shown at the object's places, and here `value <= limit` is not.
#include "counter.hpp"

verified unsigned room(unsigned start)
    ensures (result == 10u - start)
{
    const Counter counter{start, 10u};
    return counter.headroom();
}

int main() {
    return static_cast<int>(room(3u));
}
