// SPEC: STDMODEL-015, VERIFIED-039, VERIFIED-040
// A callee that may write through a pointer may write storage a reference
// argument designates, and so any vector that may be that one: a span over
// such a vector is stale after the call, and the diagnostic names the call
// that ended its generation. Twin of `view_of_local_kept` in `containers.cpp`.
#include <cstddef>
#include <span>
#include <vector>

verified void peek(const std::vector<unsigned>& x, unsigned& w, unsigned* p)
    expects (writable(p))
    ensures (true)
{
    w = 1u;
    *p = 0u;
}

verified std::size_t view_of_alias(const std::vector<unsigned>& a, const std::vector<unsigned>& c, unsigned* p)
    expects (writable(p))
    ensures (result == result)
{
    std::span<const unsigned> s(c);
    unsigned w = 0u;
    peek(a, w, p);
    return s.size();
}

int main() {
    return 0;
}
