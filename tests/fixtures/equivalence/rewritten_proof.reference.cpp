// Ordinary C++: `rewritten_proof.cpp` erased by hand as SPEC.md Annex M says it erases.
// Every Law and proof is gone whole, `pure` and `verified` leave their
// functions as written, and every clause is gone.
#include <iostream>

unsigned identity(unsigned x) {
    return x;
}

int main() {
    std::cout << identity(41u) << "\n";
    return 0;
}
