// SPEC: LOOP-002, ARITH-006
// `read_number` of integration/text.cpp without the guard before the next
// digit: seven digits make a value past 999999, so the loop invariant bounding
// the value is not preserved, and neither is the postcondition resting on it.
#include <cstddef>
#include <span>

verified long long read_number(std::span<const char> in, std::size_t from, std::size_t to)
    expects (readable(in) && from <= to && to <= in.size())
    ensures (-1ll <= result && result <= 999999ll)
{
    if (from == to) {
        return -1ll;
    }
    long long value = 0ll;
    std::size_t i = from;
    while (i < to)
        invariant (from <= i && i <= to && 0ll <= value && value <= 999999ll)
        decreases (to - i)
    {
        const char c = in[i];
        if (c < '0' || c > '9') {
            return -1ll;
        }
        value = value * 10ll + (c - '0');
        ++i;
    }
    return value;
}
