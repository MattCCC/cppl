// SPEC: STDMODEL-015
// After `pop_back` an element is formed again, against the new length: the
// guard written before the pop bounds nothing after it. Accepted twin:
// `vector_after_pop`, which guards after the pop.
#include <cstddef>
#include <vector>

verified unsigned read_after_pop(std::size_t i)
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    if (i < v.size()) {
        v.pop_back();
        return v[i];
    }
    return 0u;
}

int main() {
    return 0;
}
