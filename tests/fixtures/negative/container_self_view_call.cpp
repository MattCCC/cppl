// SPEC: STDMODEL-016
// A span of a vector and the vector itself by mutable reference, in one call:
// the callee could reallocate the storage its capability designates. Accepted
// twin: `span_calls`, which passes a different vector by reference.
#include <cstddef>
#include <span>
#include <vector>

verified std::size_t collect(std::span<const unsigned> in, std::vector<unsigned>& out)
    expects (readable(in))
    ensures (result == in.size())
{
    std::size_t i = 0ul;
    while (i < in.size())
        invariant (i <= in.size())
    {
        out.push_back(in[i]);
        ++i;
    }
    return i;
}

verified std::size_t into_itself()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    return collect(v, v);
}

int main() {
    return 0;
}
