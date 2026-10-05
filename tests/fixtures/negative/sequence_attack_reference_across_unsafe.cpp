// SPEC: STDMODEL-025, UNSAFE-003
// An unsafe block that may reach a vector is a new storage generation of it: an
// element reference formed before the block is not used after it. Accepted
// twin: `read_before_unsafe` in sequence_attacks.cpp.
#include <cstddef>
#include <vector>

void grow(std::vector<unsigned>& v);

verified unsigned across_unsafe()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u};
    unsigned& r = v[0];
    unsafe {
        grow(v);
    }
    return r;
}

void grow(std::vector<unsigned>& v) {
    v.push_back(2u);
}

int main() {
    return 0;
}
