// SPEC: STDMODEL-019
// `resize` is not a modeled operation: it is refused, never approximated.
// Accepted twin: `grown_by_push` in sequence_attacks.cpp, which reaches the same
// length through the modeled `push_back`.
#include <cstddef>
#include <vector>

verified std::size_t resized()
    ensures (result == 4ul)
{
    std::vector<unsigned> v{1u, 2u};
    v.resize(4ul);
    return v.size();
}

int main() {
    return 0;
}
