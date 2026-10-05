// SPEC: CONSTRUCT-096
// RFC 0022, the V1 verified subset: a verified body may use this construct, break statement,
// and it is modeled. Its refused twin is negative/subset/x096_break_statement.cpp.

verified unsigned probe(unsigned n)
    ensures (result <= n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
        decreases (n - i)
    {
        if (i == 5u) {
            break;
        }
        i = i + 1u;
    }
    return i;
}

int main() {
    return probe(3u) == 3u ? 0 : 1;
}
