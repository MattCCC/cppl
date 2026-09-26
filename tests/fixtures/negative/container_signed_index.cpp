// SPEC: STDMODEL-012, ARITH-008
// A signed index is converted to the size type modularly: a negative `i`
// becomes an index far past the end, which `i < 3` does not bound. The bound
// is owed on the converted value. Accepted twin: `signed_guarded`, which also
// requires `0 <= i`.
#include <cstddef>
#include <vector>

verified unsigned unguarded_sign(const std::vector<unsigned>& v, int i)
    expects (v.size() == 3ul)
    ensures (result == result)
{
    if (i < 3) {
        return v[i];
    }
    return 0u;
}

int main() {
    return 0;
}
