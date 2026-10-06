// SPEC: CONSTRUCT-071
// RFC 0022, the V1 verified subset: a verified body may use this construct, alignof,
// and it is modeled. Its refused twin is negative/subset/x071_alignof.cpp.

verified unsigned long probe()
    ensures (result == 4ul)
{
    return alignof(unsigned);
}

int main() {
    return probe() == 4ul ? 0 : 1;
}
