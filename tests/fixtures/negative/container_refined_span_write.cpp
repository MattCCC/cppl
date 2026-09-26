// SPEC: STDMODEL-020
// A write through a span is a write to the elements it views, which owe their
// container's refinement whatever the span's element type is spelled as.
// Accepted twin: `vector_refined`.
#include <cstddef>
#include <span>
#include <vector>

type Positive = unsigned where (self > 0u);

verified void write_through_view()
    ensures (true)
{
    std::vector<Positive> v{1u, 2u};
    std::span<unsigned> s(v);
    if (0ul < s.size()) {
        s[0] = 0u;
    }
}

int main() {
    return 0;
}
