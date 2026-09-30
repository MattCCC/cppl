// SPEC: STDMODEL-012
// Index 0 of an empty vector is past its end. Accepted twin: `first_of_one` in
// sequence_attacks.cpp.
#include <cstddef>
#include <vector>

verified unsigned first_of_empty()
    ensures (result == result)
{
    std::vector<unsigned> v;
    return v[0];
}

int main() {
    return 0;
}
