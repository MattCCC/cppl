// SPEC: STDMODEL-019
// A member the model does not state is refused by name, never approximated.
#include <vector>

verified unsigned checked_access()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u};
    return v.at(0ul);
}

int main() {
    return 0;
}
