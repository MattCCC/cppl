// SPEC: STDMODEL-015
// A reference to an element, read after `push_back` may have reallocated the
// storage it designates, is a stale pointer. Accepted twin: `vector_reference`,
// which reads it before the push.
#include <vector>

verified unsigned stale_reference()
    ensures (result == 3u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[1];
    r = 3u;
    v.push_back(4u);
    return r;
}

int main() {
    return 0;
}
