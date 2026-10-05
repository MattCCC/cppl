// SPEC: STDMODEL-012
// `i <= size()` admits `i == size()`, one past the last element. Accepted twin:
// `below_size` in sequence_attacks.cpp, guarded by `i < size()`.
#include <cstddef>
#include <vector>

verified unsigned off_by_one(const std::vector<unsigned>& v, std::size_t i)
    ensures (result == result)
{
    if (i <= v.size()) {
        return v[i];
    }
    return 0u;
}

int main() {
    return 0;
}
