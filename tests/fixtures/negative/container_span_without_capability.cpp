// SPEC: STDMODEL-016
// A span does not make the storage it views valid by existing: reading one of
// its elements needs `readable`. Accepted twin: `span_at`.
#include <cstddef>
#include <span>

verified unsigned span_unstated(std::span<const unsigned> in, std::size_t i)
    expects (i < in.size())
    ensures (result == result)
{
    return in[i];
}

int main() {
    return 0;
}
