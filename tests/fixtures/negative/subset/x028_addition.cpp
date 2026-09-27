// SPEC: CONSTRUCT-028
// RFC 0022: the refused twin of subset/x028_addition.cpp (addition), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified int probe(int a, int b)
    expects (a >= 0 && a <= 1000 && b >= 0 && b <= 2147483647)
    ensures (result == a + b)
{
    return a + b;
}

int main() { return probe(2, 3) == 5 ? 0 : 1; }
