// SPEC: STDMODEL-020
// A copy carries values, never a proof that they satisfy a refinement the
// source never owed.
#include <vector>

type Positive = unsigned where (self > 0u);

verified void launder()
    ensures (true)
{
    std::vector<unsigned> plain{0u};
    std::vector<Positive> refined = plain;
}

int main() {
    return 0;
}
