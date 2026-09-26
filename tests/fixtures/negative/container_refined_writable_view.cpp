// SPEC: STDMODEL-020
// A callee handed a writable span of a refined vector could store a value that
// does not satisfy its refinement: nothing in its contract obliges otherwise.
#include <span>
#include <vector>

type Positive = unsigned where (self > 0u);

verified void overwrite(std::span<unsigned> s)
    expects (writable(s))
    ensures (true)
{
    if (!s.empty()) {
        s[0] = 0u;
    }
}

verified void hand_out()
    ensures (true)
{
    std::vector<Positive> v{1u, 2u};
    overwrite(v);
}

int main() {
    return 0;
}
