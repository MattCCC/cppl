// SPEC: STDMODEL-012
// A character of a string is read only within its length. Accepted twin:
// `string_character`.
#include <cstddef>
#include <string>

verified char string_unguarded(const std::string& s, std::size_t i)
    ensures (true)
{
    return s[i];
}

int main() {
    return 0;
}
