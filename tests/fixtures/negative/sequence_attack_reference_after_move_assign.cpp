// SPEC: STDMODEL-015, STDMODEL-025
// Move assignment hands the vector another vector's storage: a reference into
// the old storage designates nothing the vector still owns. Accepted twin:
// `reference_before_move_assign` in sequence_attacks.cpp.
#include <cstddef>
#include <utility>
#include <vector>

verified unsigned reference_after_move_assign()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{3u};
    unsigned& r = v[0];
    v = std::move(w);
    return r;
}

int main() {
    return 0;
}
