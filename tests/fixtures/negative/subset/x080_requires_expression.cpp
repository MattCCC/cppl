// SPEC: CONSTRUCT-080
// RFC 0022: the refused twin of subset/x080_requires_expression.cpp (requires expression), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    constexpr bool addable = requires(unsigned a) { a + 1u; };
    if (addable) {
        return x + 1u;
    }
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
