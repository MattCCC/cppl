// SPEC: STDMODEL-012, STDMODEL-016
// A capability makes a span's elements readable; it bounds no index. Accepted
// twin: `span_at`, which also states `i < in.size()`.
#include <cstddef>
#include <span>

verified unsigned span_unbounded(std::span<const unsigned> in, std::size_t i)
    expects (readable(in))
    ensures (result == result)
{
    return in[i];
}

int main() {
    return 0;
}
