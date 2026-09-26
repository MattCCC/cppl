// SPEC: STDMODEL-020
// A container whose element type is refined, handed by mutable reference to a
// callee that may leave any value in it. Before this was refused, `f` was
// reported proven and returned 0. Accepted twin: `refined_by_const_reference`
// in `fixtures/container_crossings.cpp`.
#include <cstddef>
#include <vector>

type Positive = unsigned where (self > 0u);

verified void g(std::vector<unsigned>& v)
    ensures (true)
{
    if (v.size() > 0ul) {
        v[0] = 0u;
    }
}

verified unsigned f()
    ensures (result > 0u)
{
    std::vector<Positive> r{1u};
    g(r);
    if (r.size() > 0ul) {
        return r[0];
    }
    return 1u;
}

int main() {
    return static_cast<int>(f());
}
