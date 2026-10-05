// Ordinary C++ with a type alias named `type` and an initializer or a later
// declarator spelling `where (...)` (SPEC.md 3.1, GRAMMAR.md 14).
//
// A refinement's `where` follows its base type. After `,` or an operator it is
// the next declarator of a declaration, or an operand of its initializer.
#include <cstdio>

using type = int;

type a = 5, where(6);

namespace other {
int where(int x) {
    return x * 10;
}
type sum = 1 + where(2);
type chosen = sum > 0 ? where(3) : 0;
} // namespace other

int main() {
    type b = 7, where(8);
    type c = b - other::where(1);
    std::printf("%d %d %d %d %d %d %d\n", a, ::where, b, where, c, other::sum, other::chosen);
    return 0;
}
