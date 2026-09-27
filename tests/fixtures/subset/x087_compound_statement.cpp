// SPEC: CONSTRUCT-087
// RFC 0022, the V1 verified subset: a verified body may use this construct, compound statement,
// and it is modeled. Its refused twin is negative/subset/x087_compound_statement.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    {
        unsigned inner = x;
        return inner;
    }
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
