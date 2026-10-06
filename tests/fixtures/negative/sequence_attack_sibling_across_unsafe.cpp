// SPEC: UNSAFE-003, STDMODEL-015
// An unsafe block that names one element reaches the whole container: from the
// element's address, pointer arithmetic to a sibling is valid C++. What was read
// of the sibling before the block is not what it holds after. Accepted twin:
// `read_before_unsafe` in sequence_attacks.cpp.
#include <array>
#include <cstddef>
#include <vector>

verified unsigned sibling_of_vector_element()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    const unsigned before = v[1];
    unsafe {
        (&r)[1] = 9u;
    }
    const unsigned after = v[1];
    return after - before;
}

verified unsigned sibling_of_array_element()
    ensures (result == 2u)
{
    std::array<unsigned, 2> a{1u, 2u};
    unsigned& r = a[0];
    unsafe {
        (&r)[1] = 9u;
    }
    return a[1];
}

struct Pair {
    unsigned x;
    unsigned y;
};

verified unsigned sibling_of_member()
    ensures (result == 2u)
{
    Pair p{1u, 2u};
    unsigned& r = p.x;
    unsafe {
        (&r)[1] = 9u;
    }
    return p.y;
}

int main() {
    return 0;
}
