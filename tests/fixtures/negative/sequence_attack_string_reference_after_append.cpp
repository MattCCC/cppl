// SPEC: STDMODEL-015, STDMODEL-025
// Appending to a string may reallocate it, so a character reference formed
// before is not used after. Accepted twin: `character_before_append` in
// sequence_attacks.cpp.
#include <cstddef>
#include <string>

verified char stale_character()
    ensures (result == result)
{
    std::string s = "ab";
    char& c = s[0];
    s += 'c';
    return c;
}

int main() {
    return 0;
}
