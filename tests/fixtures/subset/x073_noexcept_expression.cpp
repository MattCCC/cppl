// SPEC: CONSTRUCT-073
// RFC 0022, the V1 verified subset: a verified body may use this construct, noexcept expression,
// and it is modeled. Its refused twin is negative/subset/x073_noexcept_expression.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    if (noexcept(x + 1u)) {
        return x;
    }
    return x + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
