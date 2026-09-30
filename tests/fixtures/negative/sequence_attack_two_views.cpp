// SPEC: STDMODEL-016, VERIFIED-039, VERIFIED-040
// Two span parameters may view one storage, so a write through one may change
// what the other reads. Accepted twin: `written_view` in sequence_attacks.cpp,
// which reads back the view it wrote.
#include <cstddef>
#include <span>

verified unsigned through_two(std::span<unsigned> a, std::span<const unsigned> b)
    expects (readable(a) && writable(a) && readable(b) && 0ul < a.size() && 0ul < b.size())
    ensures (result == 0u)
{
    const unsigned before = b[0];
    a[0] = 7u;
    return b[0] - before;
}

int main() {
    return 0;
}
