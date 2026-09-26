// SPEC: STDMODEL-014
// A span declared outside a block cannot be pointed at a vector the block
// destroys: a span is formed only by its declaration's initializer, over a
// container that outlives it, and is never assigned. Accepted twin:
// `span_local`.
#include <cstddef>
#include <span>
#include <vector>

verified unsigned outlived()
    ensures (result == result)
{
    std::span<const unsigned> s;
    {
        std::vector<unsigned> v{1u, 2u};
        s = v;
    }
    if (0ul < s.size()) {
        return s[0];
    }
    return 0u;
}

int main() {
    return 0;
}
