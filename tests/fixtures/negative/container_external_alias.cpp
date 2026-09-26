// SPEC: STDMODEL-015
// Two references to vectors may name one vector: appending through one may
// change the length read through the other.
#include <cstddef>
#include <vector>

verified std::size_t aliased(const std::vector<unsigned>& a, std::vector<unsigned>& b)
    ensures (result == 0ul)
{
    const std::size_t before = a.size();
    b.push_back(1u);
    return a.size() - before;
}

int main() {
    std::vector<unsigned> v{1u};
    return aliased(v, v) == 0ul ? 0 : 1;
}
