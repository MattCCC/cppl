// SPEC: STDMODEL-016, STDMODEL-017
// An element by mutable reference and a writable span of its own container, in
// one call. Accepted twin: `element_and_other_span` in
// `fixtures/container_crossings.cpp`.
#include <cstddef>
#include <span>
#include <vector>

verified unsigned touch(unsigned& x, std::span<unsigned> s)
    ensures (result == 0u)
{
    return 0u;
}

verified unsigned same_call()
    ensures (result == 0u)
{
    std::vector<unsigned> v{4u, 5u, 6u};
    return touch(v[0], v);
}

int main() {
    return 0;
}
