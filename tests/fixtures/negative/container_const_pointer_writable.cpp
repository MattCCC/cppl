// SPEC: VERIFIED-036, STDMODEL-017
// A pointer to const grants no write. Accepted twin: `element_and_readable_data`
// in `fixtures/container_crossings.cpp`, which asks `readable` of one.
#include <cstddef>

verified unsigned w(const unsigned* p, std::size_t n)
    expects (writable(p, n))
    ensures (true)
{
    return 0u;
}

int main() {
    return 0;
}
