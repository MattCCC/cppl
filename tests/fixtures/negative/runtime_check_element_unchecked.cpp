// SPEC: RUNTIMECHECK-007, STDMODEL-020
// The element is pushed on the path where the check failed as well as where
// it held. Twin of `checked_elements`.
#include <vector>

type Positive = int where (self > 0);

verified unsigned unchecked_element(int raw)
    ensures (result > 0u)
{
    std::vector<Positive> values;
    if (raw > 0) {
        values.push_back(1);
    }
    values.push_back(raw);
    return static_cast<unsigned>(values.size());
}

int main() {
    return static_cast<int>(unchecked_element(-4));
}
