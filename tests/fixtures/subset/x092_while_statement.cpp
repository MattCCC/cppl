// SPEC: CONSTRUCT-092
// RFC 0022, the V1 verified subset: a verified body may use this construct, while statement,
// and it is modeled. Its refused twin is negative/subset/x092_while_statement.cpp.

verified unsigned probe(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
        decreases (n - i)
    {
        i = i + 1u;
    }
    return i;
}

int main() {
    return probe(3u) == 3u ? 0 : 1;
}
