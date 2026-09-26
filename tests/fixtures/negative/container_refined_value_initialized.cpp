// SPEC: STDMODEL-020
// A sized vector holds value-initialized elements, which enter the element
// type: `0` does not satisfy `Positive`.
#include <vector>

type Positive = unsigned where (self > 0u);

verified void zeros()
    ensures (true)
{
    std::vector<Positive> v(2ul);
}

int main() {
    return 0;
}
