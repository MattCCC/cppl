// SPEC: STDMODEL-013, STDMODEL-023
// `pop_back` owes a non-empty vector, as a callee's precondition is owed.
// Accepted twin: `vector_pop_guarded`.
#include <cstddef>
#include <vector>

verified std::size_t pop_unguarded(std::vector<unsigned>& v)
    ensures (result == result)
{
    v.pop_back();
    return v.size();
}

int main() {
    return 0;
}
