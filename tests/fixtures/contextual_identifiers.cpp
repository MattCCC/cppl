// Ordinary C++ that happens to use C++L contextual words as identifiers.
// None of this may become C++L syntax (SPEC.md 3.1, GRAMMAR.md 50).

int law = 1;

void proof() {}

struct ghost {};

int verified = 0;

int trusted = 0;

int main() {
    proof();
    ghost value;
    (void)value;
    (void)verified;
    (void)trusted;
    return law - 1;
}
