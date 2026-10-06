// SPEC: CONSTRUCT-070
// RFC 0022, the V1 verified subset: a verified body may use this construct, sizeof,
// and it is modeled. Its refused twin is negative/subset/x070_sizeof.cpp.

verified unsigned long probe()
    ensures (result == 4ul)
{
    return sizeof(unsigned);
}

int main() { return probe() == 4ul ? 0 : 1; }
