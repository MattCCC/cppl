// SPEC: STDMODEL-011
// An element of a std::array a reference designates is caller storage another
// reference may write during the call, so it is not read as the value it had
// on entry.
#include <array>

verified unsigned through_reference(const std::array<unsigned, 2>& a, unsigned& x)
    ensures (result == 0u)
{
    unsigned before = a[0];
    x = before + 1u;
    return a[0] - before;
}

int main() {
    return 0;
}
