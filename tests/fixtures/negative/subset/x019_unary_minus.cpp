// SPEC: CONSTRUCT-019
// RFC 0022: the refused twin of subset/x019_unary_minus.cpp (unary minus), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified int probe(int x)
    expects (x < 100)
    ensures (result == -x)
{
    return -x;
}

int main() { return probe(2) == -2 ? 0 : 1; }
