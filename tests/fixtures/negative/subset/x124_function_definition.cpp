// SPEC: CONSTRUCT-124
// RFC 0022: the refused twin of subset/x124_function_definition.cpp (function definition), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return x + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
