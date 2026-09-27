// SPEC: CONSTRUCT-057
// RFC 0022: the refused twin of subset/x057_direct_function_call.cpp (direct function call), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned successor(unsigned x)
    expects (x < 100u)
    ensures (result == x + 1u)
{
    return x + 1u;
}

verified unsigned probe(unsigned x)
    expects (x < 200u)
    ensures (result == x + 1u)
{
    return successor(x);
}

int main() { return probe(2u) == 3u ? 0 : 1; }
