// SPEC: ERASE-005, ERASE-017
// A directive between a function's contract clauses stays where it stands, so
// once the clauses are erased it stands between the declarator and the body,
// where C++ admits no `#pragma`. The program erased by hand is refused by Clang
// for that reason, and so is this one, with the same message, rather than
// compiled with the directive moved or dropped.
#include <cstdio>

verified unsigned id(unsigned x)
    expects (x < 100u)
#pragma pack(push, 1)
    ensures (result == x)
{
    return x;
}

struct Packed {
    char c;
    int i;
};
#pragma pack(pop)

int main() {
    std::printf("%zu %u\n", sizeof(Packed), id(3u));
}
