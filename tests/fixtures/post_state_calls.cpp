// The accepted half of the matched pair whose refused half is
// `negative/post_state_after_returned_call.cpp` (SPEC.md VERIFIED-031).
//
// A function whose normal return hands storage back to its caller, and whose
// returned value is a call: the obligation relates the post-state of that
// storage and the call's result, each at its own binder.
#include <cstdio>

verified unsigned five()
    ensures (result == 5u)
{
    return 5u;
}

// The post-state of `r` is the value written to it, and the result is the
// call's.
verified unsigned writes_then_calls(unsigned& r)
    ensures (r == 5u && result == 5u)
{
    r = 5u;
    return five();
}

// The post-state of `r` is the value it arrived with, which the call does not
// change.
verified unsigned keeps_then_calls(unsigned& r)
    expects (r == 2u)
    ensures (r == 2u && result == 5u)
{
    return five();
}

int main() {
    unsigned x = 1u;
    unsigned y = 2u;
    const unsigned first = writes_then_calls(x);
    const unsigned second = keeps_then_calls(y);
    std::printf("%u %u %u %u\n", first, x, second, y);
    return 0;
}
