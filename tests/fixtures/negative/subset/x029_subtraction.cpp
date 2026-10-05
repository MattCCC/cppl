// SPEC: CONSTRUCT-029
// RFC 0022: the refused twin of subset/x029_subtraction.cpp (subtraction), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified int probe(int a, int b)
    expects (a <= 1000 && b >= 0 && b <= 1000)
    ensures (result == a - b)
{
    return a - b;
}

int main() { return probe(5, 3) == 2 ? 0 : 1; }
