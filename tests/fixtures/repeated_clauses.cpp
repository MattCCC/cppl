// At most one clause of each kind (SPEC.md CONTRACT-003).
//
// Conjoined predicates belong in a single '&&' expression, so a repeated clause
// is refused rather than silently conjoined, overwritten or dropped. Each of
// these is a syntax error, reported where the repeat was written. They share a
// fixture because one such error stops the pipeline before proof checking, so
// bundling them with semantic refusals would mask those instead of testing them.
#include <iostream>

pure unsigned identity(unsigned x) { return x; }

// A Law states one precondition and one conclusion.
law two_preconditions(unsigned x)
    expects (x > 0u)
    expects (x < 10u)
    proves (identity(x) == x);

// A verified function's clauses are 'expects', 'ensures', 'decreases'.
verified unsigned two_postconditions(unsigned x)
    ensures (result == x)
    ensures (result >= x)
{
    return x;
}

// 'expects' states what the caller owes, so it precedes the conclusion clause.
verified unsigned precondition_after_conclusion(unsigned x)
    ensures (result == x)
    expects (x < 10u)
{
    return x;
}

// A loop's clauses are 'invariant', 'decreases'.
verified unsigned two_invariants(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
        invariant (i >= 0u)
    {
        i = i + 1u;
    }
    return i;
}

// A measure list is lexicographic and must not be merged as a conjunction, so a
// second 'decreases' is refused too rather than being read as one measure.
verified unsigned two_measures(unsigned n)
    ensures (result == 0u)
{
    unsigned left = n;
    while (left > 0u)
        invariant (left <= n)
        decreases (left)
        decreases (n - left)
    {
        left = left - 1u;
    }
    return left;
}

int main() {
    std::cout << identity(41u) << "\n";
    return 0;
}
