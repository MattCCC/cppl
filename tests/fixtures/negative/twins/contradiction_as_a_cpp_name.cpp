// Refused twin of fixtures/contradiction_as_a_cpp_name.cpp (tests/negative/refused_twins.sh): the same
// program, except that keeps claims x + 1u.
// `contradiction` as an ordinary C++ name (SPEC.md 3.1, WORD-002).
//
// Here `contradiction` names a type, so `contradiction verdict(x);` in the
// verified body below is a declaration of a local, exactly as C++ reads it. A
// claim that a path cannot occur has the same spelling, and C++ comes first: in
// a translation unit that uses the word for anything else, the statement is
// never read as a claim, and the compiler says so with a warning.
#include <cstdio>

using contradiction = unsigned;

verified unsigned keeps(unsigned x)
    ensures (result == x + 1u)
{
    contradiction verdict(x);
    return verdict;
}

int main() {
    std::printf("%u\n", keeps(4u));
}
