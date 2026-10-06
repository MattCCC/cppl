// SPEC: STDMODEL-012
// An element read in an `if` condition is formed where the condition reads it,
// and owes its bound there: with nothing bounding `i`, the read is refused.
// Accepted twin: `digit_at` in fixtures/conditions.cpp, whose precondition
// bounds it.
#include <cstddef>
#include <span>

verified bool is_digit_at(std::span<const char> in, std::size_t i)
    expects (readable(in))
    ensures (result == result)
{
    if (in[i] < '0' || in[i] > '9') {
        return false;
    }
    return true;
}

int main() {
    return 0;
}
