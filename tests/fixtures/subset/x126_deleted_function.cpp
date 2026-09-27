// SPEC: CONSTRUCT-126
// RFC 0022, the V1 verified subset: a verified body may use this construct, deleted function,
// and it is modeled. Its refused twin is negative/subset/x126_deleted_function.cpp.

unsigned narrow(double) = delete;

verified unsigned narrow(unsigned x)
    ensures (result == x)
{
    return x;
}

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return narrow(x);
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
