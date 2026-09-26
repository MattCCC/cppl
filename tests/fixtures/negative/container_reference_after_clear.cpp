// SPEC: STDMODEL-015
// `clear` destroys every element: a reference to one is dangling afterwards.
// Accepted twin: `vector_reference`.
#include <vector>

verified void destroyed_element()
    ensures (true)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    v.clear();
    r = 5u;
}

int main() {
    return 0;
}
