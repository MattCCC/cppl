// Ordinary C++: `formal_equality.cpp` erased by hand as SPEC.md Annex M says it erases.
// Every Law and proof is gone whole, `pure` and `verified` leave their
// functions as written, and every clause is gone.
#include <cstdio>

// Ordinary C++ names stay ordinary outside formal contexts.
template <class T> int Eq(T, T) {
    return 7;
}
template <class T> struct IdentityType {
    using type = T;
};
using U = unsigned;
U identity(U x) {
    return x;
}
int identity(int x) {
    return x;
}

U keep(U x) {
    return x;
}

int main() {
    std::printf("%u %d\n", keep(2u), Eq(1, 2));
}
