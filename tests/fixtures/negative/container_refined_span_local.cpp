// SPEC: STDMODEL-020
// A span's elements are the storage it views: a refinement written as its own
// element type would state nothing of that storage. Accepted twin:
// `span_local` in `fixtures/containers.cpp`, whose span of a vector is plain.
#include <cstddef>
#include <span>
#include <vector>

type Positive = unsigned where (self > 0u);

verified unsigned first()
    ensures (result > 0u)
{
    std::vector<unsigned> v{0u};
    std::span<const Positive> s(v);
    if (s.size() > 0ul) {
        return s[0];
    }
    return 1u;
}

int main() {
    return 0;
}
