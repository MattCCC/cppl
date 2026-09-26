// SPEC: STDMODEL-014, STDMODEL-015
// A returned span outlives every storage the body could have formed it over.
#include <span>
#include <vector>

verified std::span<const unsigned> dangling(const std::vector<unsigned>& v)
    ensures (true)
{
    return v;
}

int main() {
    return 0;
}
