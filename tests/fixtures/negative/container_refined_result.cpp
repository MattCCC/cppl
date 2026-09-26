// SPEC: STDMODEL-020
// A refined element type is a content invariant of a local's storage, not part
// of the C++ type: a result spelled `std::vector<Positive>` is a
// `std::vector<unsigned>`, and nothing about its elements reaches a caller.
#include <vector>

type Positive = unsigned where (self > 0u);

verified std::vector<Positive> make()
    ensures (true)
{
    std::vector<unsigned> a{0u};
    return a;
}

int main() {
    return 0;
}
