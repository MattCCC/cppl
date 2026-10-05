// SPEC: CONSTRUCT-095
// RFC 0022, the V1 verified subset: a verified body may use this construct, do-while statement,
// and it is modeled. Its refused twin is negative/subset/x095_do_while_statement.cpp.

verified unsigned probe(unsigned n)
    expects (n > 0u)
    ensures (result == 0u)
{
    unsigned i = n;
    do
        invariant (i > 0u)
        decreases (i)
    {
        i = i - 1u;
    } while (i > 0u);
    return i;
}

int main() {
    return probe(3u) == 0u ? 0 : 1;
}
