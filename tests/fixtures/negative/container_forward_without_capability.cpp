// SPEC: STDMODEL-016
// A span parameter handed on to a callee requiring `readable` needs the caller
// to hold it. Accepted twin: `span_calls`.
#include <cstddef>
#include <span>

verified std::size_t count(std::span<const unsigned> in)
    expects (readable(in))
    ensures (result == in.size())
{
    return in.size();
}

verified std::size_t forward(std::span<const unsigned> in)
    ensures (result == result)
{
    return count(in);
}

int main() {
    return 0;
}
