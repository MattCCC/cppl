// SPEC: CONSTRUCT-027
// RFC 0022, the V1 verified subset: a verified body may use this construct, postfix decrement,
// and it is modeled. Its refused twin is negative/subset/x027_postfix_decrement.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x - 1u)
{
    unsigned counter = x;
    counter--;
    return counter;
}

int main() {
    return probe(2u) == 1u ? 0 : 1;
}
