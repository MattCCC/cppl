// SPEC: STDMODEL-014, UNSAFE-003
// An unsafe block that names only a span may write the vector the span views,
// although it never names that vector: what was read from the vector before the
// block is not what it holds after. Accepted twin: `read_before_unsafe` in
// sequence_attacks.cpp.
#include <cstddef>
#include <span>
#include <vector>

verified unsigned through_view()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    const unsigned before = v[0];
    unsafe {
        s[0] = 9u;
    }
    const unsigned after = v[0];
    return after - before;
}

int main() {
    return 0;
}
