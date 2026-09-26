// SPEC: STDMODEL-015
// A callee holding a vector by mutable reference may reallocate it, so a span
// formed over it before the call is stale after it.
#include <cstddef>
#include <span>
#include <vector>

verified void grow(std::vector<unsigned>& v)
    ensures (true)
{
    v.push_back(1u);
}

verified unsigned stale_after_call()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    std::span<const unsigned> s(v);
    grow(v);
    if (0ul < s.size()) {
        return s[0];
    }
    return 0u;
}

int main() {
    return 0;
}
