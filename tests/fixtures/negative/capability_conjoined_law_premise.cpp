// SPEC: STDMODEL-016
// A law's premise conjoining a capability with a predicate is refused: read
// as its predicate alone, the trusted law would be assumed under less than it
// states.
#include <cstddef>

trusted law positive_when_readable(unsigned* p, unsigned x)
    expects (readable(p) && x > 1u)
    proves (x > 0u);

int main() {
    return 0;
}
