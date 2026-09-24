// A termination obligation is never accepted unchecked (SPEC.md
// TERMINATION-004, TERMINATION-006, TERMINATION-007).
//
// This is the safety-critical direction. Accepting a 'decreases' clause whose
// descent was not shown would let a trust report describe a program as total
// when nothing established that it terminates.
#include <iostream>

// The recursive call is made at the measure the function was entered with.
verified unsigned countdown(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return countdown(n);
}

// A lexicographic measure compares its first component first: here it is the
// column, which the second branch resets upward.
verified unsigned nested(unsigned rows, unsigned columns)
    ensures (result == 0u)
{
    unsigned r = rows;
    unsigned c = columns;
    while (r > 0u)
        invariant (r <= rows && c <= columns)
        decreases (c, r)
    {
        if (c > 0u) {
            c = c - 1u;
        } else {
            r = r - 1u;
            c = columns;
        }
    }
    return r;
}

int main() {
    std::cout << countdown(3u) << nested(2u, 2u) << "\n";
    return 0;
}
