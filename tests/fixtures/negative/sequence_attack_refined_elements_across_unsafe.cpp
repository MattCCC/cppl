// SPEC: STDMODEL-020, UNSAFE-003
// A refined element type is a content invariant of the vector's storage, and an
// unsafe block that reaches the vector -- here through a reference to one
// element -- may leave any value in any element. Nothing re-establishes the
// invariant, so the block is refused rather than the invariant kept.
#include <cstddef>
#include <vector>

type Positive = unsigned where (self > 0u);

verified unsigned refined_sibling()
    ensures (result > 0u)
{
    std::vector<Positive> v{1u, 2u};
    const unsigned& r = v[0];
    unsafe {
        const_cast<unsigned*>(&r)[1] = 0u;
    }
    return v[1];
}

int main() {
    return 0;
}
