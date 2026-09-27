// Ordinary C++: `logical_composition.cpp` erased by hand as SPEC.md Annex M says it erases.
// Every Law and proof is gone whole, `pure` and `verified` leave their
// functions as written, and every clause is gone.
#include <cstdio>

unsigned identity(unsigned x) {
    return x;
}

// An ordinary C++ template may be named `Eq`. It keeps its runtime meaning
// below; inside a proposition the spelling is the formal form.
template <class T> bool Eq(T a, T b) {
    return a == b;
}

unsigned keep(unsigned x) {
    return x;
}

int main() {
    std::printf("%u %d\n", keep(7u), Eq<int>(2, 2) ? 1 : 0);
}
