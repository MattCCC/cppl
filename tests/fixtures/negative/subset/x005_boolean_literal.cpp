// SPEC: CONSTRUCT-005
// RFC 0022: the refused twin of subset/x005_boolean_literal.cpp (boolean literal), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    bool flag = false;
    if (flag) {
        return x;
    }
    return 0u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
