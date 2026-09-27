// SPEC: CONSTRUCT-093
// RFC 0022: the refused twin of subset/x093_classic_for_statement.cpp (classic for statement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned n)
    ensures (result == n)
{
    unsigned last = 0u;
    for (unsigned i = 0u; i < n; ++i)
        invariant ((i <= n) && (last == i))
        decreases (n - i)
    {
        last += 2u;
    }
    return last;
}

int main() { return probe(3u) == 3u ? 0 : 1; }
