// SPEC: CONSTRUCT-019
// RFC 0022, the V1 verified subset: a verified body may use this construct, unary minus,
// and it is modeled. Its refused twin is negative/subset/x019_unary_minus.cpp.

verified int probe(int x)
    expects (x > -100 && x < 100)
    ensures (result == -x)
{
    return -x;
}

int main() {
    return probe(2) == -2 ? 0 : 1;
}
