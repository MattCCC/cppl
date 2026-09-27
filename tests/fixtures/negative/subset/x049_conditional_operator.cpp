// SPEC: CONSTRUCT-049
// RFC 0022: the refused twin of subset/x049_conditional_operator.cpp (conditional operator), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result >= 5u)
{
    return x > 5u ? x : 4u;
}

int main() { return probe(2u) == 5u ? 0 : 1; }
