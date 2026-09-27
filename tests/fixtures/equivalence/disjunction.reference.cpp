// Ordinary C++: `disjunction.cpp` erased by hand as SPEC.md Annex M says it erases.
// Every Law and proof is gone whole, `pure` and `verified` leave their
// functions as written, and every clause is gone.
#include <cstdio>

unsigned identity(unsigned x) {
    return x;
}

// Contracts carry disjunctions too: this one is supposed by cases at the call
// site below and proven from each side here.
unsigned narrow(unsigned x) {
    return x;
}

unsigned call_narrow(unsigned x) {
    return narrow(x);
}

int main() {
    std::printf("%u %u\n", narrow(2u), call_narrow(1u));
}
