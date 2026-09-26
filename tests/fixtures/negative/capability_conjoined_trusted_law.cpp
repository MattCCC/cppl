// SPEC: STDMODEL-016
// A trusted law admitting a memory proposition admits it on its own. One
// conjoined with a predicate is refused whole, never admitted in part.
#include <cstddef>

trusted law readable_and_one(unsigned* p, unsigned x)
    proves (readable(p) && x == 1u);

int main() {
    return 0;
}
