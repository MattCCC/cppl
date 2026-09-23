// `contradiction` named only by an included header (SPEC.md 3.1, WORD-002).
//
// Nothing in this file uses the word except the statement in the verified body,
// so a reading of this file alone takes `contradiction verdict(x);` for a claim
// that the path cannot occur. The header declares `contradiction` as a type,
// and it is the translation unit that decides: the statement declares a local,
// exactly as C++ reads it, the compiler warns that it is not a claim, and an
// editor must not color it as one.
#include "include/contradiction_type.hpp"

#include <cstdio>

proof same(unsigned v)
    proves (v == v)
{
    refl;
}

verified unsigned keeps(unsigned x)
    ensures (result == x)
{
    contradiction verdict(x);
    return verdict;
}

int main() {
    std::printf("%u\n", keeps(4u));
}
