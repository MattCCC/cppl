// SPEC: CONSTRUCT-093
// RFC 0022, the V1 verified subset: a verified body may use this construct, classic for statement,
// and it is modeled. Its refused twin is negative/subset/x093_classic_for_statement.cpp.

verified unsigned probe(unsigned n)
    ensures (result == n)
{
    unsigned last = 0u;
    for (unsigned i = 0u; i < n; ++i)
        invariant ((i <= n) && (last == i))
        decreases (n - i)
    {
        last += 1u;
    }
    return last;
}

int main() {
    return probe(3u) == 3u ? 0 : 1;
}
