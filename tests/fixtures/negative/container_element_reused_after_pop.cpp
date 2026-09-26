// SPEC: STDMODEL-015
// An element place formed before `pop_back` is not the place `v[i]` names
// after it: the read after the pop forms a new place and owes its bound again,
// against the new length. With `i == 2` the second read is past the end, so
// matching the place formed before would read storage that no longer holds an
// element. Accepted twin: `vector_after_pop`, which guards after the pop.
#include <cstddef>
#include <vector>

verified unsigned reread_after_pop(std::size_t i)
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    if (i < v.size()) {
        unsigned first = v[i];
        v.pop_back();
        return first + v[i];
    }
    return 0u;
}

int main() {
    return 0;
}
