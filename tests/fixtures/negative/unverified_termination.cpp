// Termination obligations this implementation does not verify are refused, not
// accepted (SPEC.md TERMINATION-004, TERMINATION-007).
//
// This is the safety-critical direction of an unimplemented feature. Accepting
// a 'decreases' clause it cannot check would let a trust report describe a
// program as total when nothing established that it terminates. Refusing costs
// the author a feature; accepting silently costs the report its meaning.
#include <iostream>

// Function-level termination (recursion) is not verified here, so the clause is
// refused rather than dropped from the contract it appears in.
verified unsigned countdown(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return countdown(n - 1u);
}

// A lexicographic measure is one clause whose parts are separated by ','. This
// implementation verifies a single measure, so a list is refused rather than
// having only its first component checked.
verified unsigned nested(unsigned rows, unsigned columns)
    ensures (result == 0u)
{
    unsigned r = rows;
    unsigned c = columns;
    while (r > 0u)
        invariant (r <= rows)
        decreases (r, c)
    {
        if (c > 0u) {
            c = c - 1u;
        } else {
            r = r - 1u;
        }
    }
    return r;
}

int main() {
    std::cout << countdown(3u) << "\n";
    return 0;
}
