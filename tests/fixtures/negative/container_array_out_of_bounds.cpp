// SPEC: STDMODEL-011
// A subscript of a std::array owes its bound: nothing here proves `i < 3`.
// Accepted twin: `array_guarded` in `fixtures/containers.cpp`.
#include <array>
#include <cstddef>

verified unsigned array_unguarded(std::size_t i)
    ensures (result == 5u)
{
    std::array<unsigned, 3> a{1u, 2u, 3u};
    a[i] = 5u;
    return a[i];
}

int main() {
    return 0;
}
