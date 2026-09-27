// Ordinary C++: `quantified_propositions.cpp` erased by hand as SPEC.md Annex M says it erases.
// Every Law and proof is gone whole, `pure` and `verified` leave their
// functions as written, and every clause is gone.
#include <cstdio>

// Ordinary C++ names stay ordinary outside the formal forms.
struct Holder {
    unsigned value;
};
unsigned forall(unsigned x) {
    return x;
}
unsigned exists(unsigned x) {
    return x + 1u;
}

unsigned identity(unsigned x) {
    return x;
}
unsigned add_one(unsigned x) {
    return x + 1u;
}

// Contracts carry the same propositions.
unsigned keep(unsigned x) {
    return x;
}

// A contract states a conjunction of what it guarantees, and supposes a
// conjunction of what it requires.
unsigned twice(unsigned x) {
    return x + x;
}

unsigned call_twice(unsigned x) {
    return twice(x);
}

unsigned preserve_path(unsigned x) {
    if (x == 0u)
        return 0u;
    return x;
}

int main() {
    Holder holder{40u};
    Holder* pointer = &holder;
    // Runtime `->` is untouched: verification never reaches into the program.
    std::printf("%u %u %u %u\n", pointer->value, forall(1u), exists(keep(2u)), preserve_path(call_twice(1u)));
}
