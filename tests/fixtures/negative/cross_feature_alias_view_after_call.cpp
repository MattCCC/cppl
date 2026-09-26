// SPEC: STDMODEL-015, CLASS-011, VERIFIED-030
// `first_kept` of fixtures/cross_feature/views.cpp with the span formed over a
// vector a second reference parameter designates. The two references may name
// one vector, so growing one may reallocate the other: the view is stale after
// the call, and the refusal names the call that may have ended its storage.
#include <span>
#include <vector>

verified void grow(std::vector<unsigned>& v)
    ensures (true)
{
    v.push_back(1u);
}

verified unsigned first_kept(std::vector<unsigned>& a, std::vector<unsigned>& b)
    expects (b.size() > 0ul)
    ensures (true)
{
    std::span<unsigned> view(b);
    grow(a);
    return view[0];
}
