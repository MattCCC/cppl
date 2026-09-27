// Ordinary C++: `instantiated_proof.cpp` erased by hand as SPEC.md Annex M says it erases.
// Every Law and proof is gone whole, `pure` and `verified` leave their
// functions as written, and every clause is gone.
#include <iostream>

unsigned identity(unsigned x) {
    return x;
}

unsigned add(unsigned a, unsigned b) {
    return a + b;
}

int main() {
    std::cout << add(identity(41u), 0u) << "\n";
    return 0;
}
