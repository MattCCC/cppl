// SPEC: CONSTRUCT-096
// RFC 0022: the refused twin of subset/x096_break_statement.cpp (break statement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
        decreases (n - i)
    {
        if (i == 5u) {
            break;
        }
        i = i + 1u;
    }
    return i;
}

int main() { return probe(3u) == 3u ? 0 : 1; }
