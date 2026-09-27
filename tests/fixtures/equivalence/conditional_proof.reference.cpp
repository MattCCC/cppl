// Ordinary C++: `conditional_proof.cpp` erased by hand as SPEC.md Annex M says it erases.
// Every Law and proof is gone whole, `pure` and `verified` leave their
// functions as written, and every clause is gone.
#include <iostream>

unsigned identity(unsigned x) {
    return x;
}

unsigned add_one(unsigned x) {
    return x + 1u;
}

int main() {
    std::cout << add_one(identity(40u)) << "\n";
    return 0;
}
