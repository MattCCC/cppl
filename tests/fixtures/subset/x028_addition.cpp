// SPEC: CONSTRUCT-028
// RFC 0022, the V1 verified subset: a verified body may use this construct, addition,
// and it is modeled. Its refused twin is negative/subset/x028_addition.cpp.

verified int probe(int a, int b)
    expects (a >= 0 && a <= 1000 && b >= 0 && b <= 1000)
    ensures (result == a + b)
{
    return a + b;
}

int main() {
    return probe(2, 3) == 5 ? 0 : 1;
}
