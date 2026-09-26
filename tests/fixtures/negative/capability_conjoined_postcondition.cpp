// SPEC: STDMODEL-016
// Only an `expects` clause reads a capability conjoined with a predicate apart.
// A postcondition stating one would be reported proven while its capability
// was never checked, so it is refused whole. Accepted twin: `span_at`, whose
// `expects` conjoins the two.
#include <cstddef>

verified unsigned read_once(unsigned* p)
    expects (readable(p))
    ensures (readable(p) && result == result)
{
    return *p;
}

int main() {
    return 0;
}
