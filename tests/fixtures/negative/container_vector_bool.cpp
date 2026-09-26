// SPEC: STDMODEL-010
// std::vector<bool> is not a sequence of elements: its operator[] yields a
// proxy object.
#include <cstddef>
#include <vector>

verified std::size_t bits()
    ensures (result == 2ul)
{
    std::vector<bool> v{true, false};
    return v.size();
}

int main() {
    return 0;
}
