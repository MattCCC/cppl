// Ordinary C++ naming variables after C++L specifiers and assigning to them
// with the alternative spellings of operators (SPEC.md 3.1, WORD-008).
//
// `and_eq` lexes as a name, so `pure and_eq mask;` looks like a type, then a
// declarator. It is `pure &= mask;`.
#include <cstdio>

int main() {
    int pure = 6;
    int verified = 5;
    int unsafe = 12;
    const int mask = 3;
    pure and_eq mask;
    verified or_eq mask;
    unsafe xor_eq mask;
    std::printf("%d %d %d\n", pure, verified, unsafe);
    return 0;
}
