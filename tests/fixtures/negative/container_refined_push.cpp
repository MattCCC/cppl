// SPEC: STDMODEL-020
// A pushed value enters the element type and owes its predicate. Accepted twin:
// `vector_refined`.
#include <vector>

type Positive = unsigned where (self > 0u);

verified void push_zero()
    ensures (true)
{
    std::vector<Positive> v{1u};
    v.push_back(0u);
}

int main() {
    return 0;
}
