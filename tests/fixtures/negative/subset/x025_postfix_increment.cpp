// SPEC: CONSTRUCT-025
// RFC 0022: the refused twin of subset/x025_postfix_increment.cpp (postfix increment), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x + 2u)
{
    unsigned counter = x;
    counter++;
    return counter;
}

int main() { return probe(2u) == 3u ? 0 : 1; }
