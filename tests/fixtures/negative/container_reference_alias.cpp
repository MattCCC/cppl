// SPEC: STDMODEL-015
// A reference parameter may designate an element of a vector a callee holds by
// mutable reference: after the call, its value is not known to be unchanged.
#include <vector>

verified void bump(std::vector<unsigned>& v)
    ensures (true)
{
    if (!v.empty()) {
        v[0] = v[0] + 1u;
    }
}

verified unsigned through_element(unsigned& r, std::vector<unsigned>& v)
    ensures (result == 0u)
{
    const unsigned before = r;
    bump(v);
    return r - before;
}

int main() {
    std::vector<unsigned> v{1u};
    return through_element(v[0], v) == 0u ? 0 : 1;
}
