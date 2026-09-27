// SPEC: CONSTRUCT-097
// RFC 0022: the refused twin of subset/x097_continue_statement.cpp (continue statement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
        decreases (n - i)
    {
        i = i + 0u;
        if (i == 2u) {
            continue;
        }
    }
    return i;
}

int main() { return probe(3u) == 3u ? 0 : 1; }
