// SPEC: STDMODEL-023
// A vector passed by value is copied into the callee's parameter: the callee
// grows its copy, never the caller's vector, which still has length 2. The
// contract claims the caller's vector grew, which is false. Accepted twin:
// `vector_copy_call`.
#include <cstddef>
#include <vector>

verified std::size_t grow_copy(std::vector<unsigned> v)
    ensures (result == v.size() + 1ul)
{
    v.push_back(1u);
    return v.size();
}

verified std::size_t grown_by_callee()
    ensures (result == 3ul)
{
    std::vector<unsigned> v{1u, 2u};
    std::size_t grown = grow_copy(v);
    return v.size();
}

int main() {
    return 0;
}
