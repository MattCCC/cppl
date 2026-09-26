// SPEC: VERIFIED-036, VERIFIED-037
// A capability over n objects says nothing of the pointer when n may be zero,
// and in particular not that it is non-null: an empty vector's data pointer
// may be null. Accepted twin: `empty_data` in `fixtures/container_crossings.cpp`.
#include <cstddef>

verified bool non_null(const unsigned* p, std::size_t n)
    expects (readable(p, n))
    ensures (result)
{
    return p != nullptr;
}

int main() {
    return 0;
}
