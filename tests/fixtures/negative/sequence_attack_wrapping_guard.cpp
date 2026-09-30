// SPEC: STDMODEL-012, ARITH-003
// `i + 1 <= size()` looks like `i < size()`, but at `i == SIZE_MAX` the sum
// wraps to 0 and the guard holds for an index past any end. Accepted twin:
// `exact_guard` in sequence_attacks.cpp.
#include <cstddef>
#include <span>

verified unsigned wrapping_guard(std::span<const unsigned> s, std::size_t i)
    expects (readable(s))
    ensures (result == result)
{
    if (i + 1ul <= s.size()) {
        return s[i];
    }
    return 0u;
}

int main() {
    return 0;
}
