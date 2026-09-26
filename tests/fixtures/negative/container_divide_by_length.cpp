// SPEC: STDMODEL-012, ARITH-004, ARITH-009
// A length may be zero, so dividing by it owes a non-empty container.
// Accepted twin: `per_element`, which divides only when the vector is not
// empty.
#include <cstddef>
#include <vector>

verified std::size_t per_element_unguarded(const std::vector<unsigned>& v, std::size_t total)
    ensures (result == result)
{
    return total / v.size();
}

int main() {
    return 0;
}
