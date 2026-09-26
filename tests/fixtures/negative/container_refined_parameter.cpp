// SPEC: STDMODEL-020
// No call could establish that every element of a vector it passes satisfies a
// refinement, and an unverified caller passes the same C++ type, so a
// container of refined elements is admitted only as a local.
#include <vector>

type Positive = unsigned where (self > 0u);

verified Positive first(const std::vector<Positive>& v)
    ensures (result > 0u)
{
    if (!v.empty()) {
        return v[0];
    }
    return 1u;
}

int main() {
    return 0;
}
