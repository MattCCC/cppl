// SPEC: STDMODEL-015, STDMODEL-025
// Copy assignment may replace the storage an element reference was formed
// over. Accepted twin: `reference_before_assign` in sequence_attacks.cpp.
#include <cstddef>
#include <vector>

verified unsigned reference_after_assign()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{3u};
    unsigned& r = v[0];
    v = w;
    return r;
}

int main() {
    return 0;
}
