// SPEC: WORD-008, ERASE-005
// Loop clauses stand between a loop's head and its braced body (GRAMMAR.md 25,
// 26). Written anywhere else they are not recognized, keep their C++ reading,
// and Clang refuses them. Nothing is erased around a loop whose invariant was
// never read, and nothing is claimed about it.
verified unsigned unbraced(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n) i = i + 1u;
    return i;
}

verified unsigned after_the_body(unsigned n)
    expects (n > 0u)
    ensures (result == n)
{
    unsigned i = 0u;
    do {
        i = i + 1u;
    } while (i < n) invariant (i <= n);
    return i;
}

int main() {
    return static_cast<int>(unbraced(2u) + after_the_body(2u));
}
