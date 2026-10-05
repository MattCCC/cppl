// SPEC: CONSTRUCT-095
// RFC 0022: the refused twin of subset/x095_do_while_statement.cpp (do-while statement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned n)
    expects (n > 0u)
    ensures (result == 0u)
{
    unsigned i = n;
    do
        invariant (i > 1u)
        decreases (i)
    {
        i = i - 1u;
    } while (i > 0u);
    return i;
}

int main() { return probe(3u) == 0u ? 0 : 1; }
