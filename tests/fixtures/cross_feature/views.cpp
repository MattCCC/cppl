// SPEC: STDMODEL-015, CLASS-011, VERIFIED-030
// A span over a local vector survives a call that takes another vector by
// mutable reference: Clang resolves the local's storage as distinct from what
// the reference parameter designates, so the common alias model keeps them
// apart and the view's storage generation is unchanged. The refused twin, a
// span over a vector another reference parameter designates, which may be the
// one the callee grows, is negative/cross_feature_alias_view_after_call.cpp.
#include <cstdio>
#include <span>
#include <vector>

verified void grow(std::vector<unsigned>& v)
    ensures (true)
{
    v.push_back(1u);
}

verified unsigned first_kept(std::vector<unsigned>& a)
    ensures (result == 7u)
{
    std::vector<unsigned> local{7u};
    std::span<unsigned> view(local);
    grow(a);
    return view[0];
}

int main() {
    std::vector<unsigned> a;
    std::printf("%u\n", first_kept(a));
    return 0;
}
