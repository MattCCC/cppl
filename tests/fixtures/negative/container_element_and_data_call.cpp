// SPEC: STDMODEL-017
// An element by mutable reference and a writable data pointer over its own
// container, in one call: the callee may write that element through either
// argument, so the call has no single post-state for it. Accepted twins:
// `element_by_const_reference_and_data` and `element_and_readable_data` in
// `fixtures/container_crossings.cpp`.
#include <cstddef>
#include <vector>

verified unsigned touch(unsigned& x, unsigned* p, std::size_t n)
    expects (writable(p, n))
    ensures (result == 0u)
{
    return 0u;
}

verified unsigned same_call()
    ensures (result == 0u)
{
    std::vector<unsigned> v{4u, 5u, 6u};
    return touch(v[0], v.data(), v.size());
}

int main() {
    return 0;
}
