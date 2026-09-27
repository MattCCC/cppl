// SPEC: CONSTRUCT-029
// RFC 0022, the V1 verified subset: a verified body may use this construct, subtraction,
// and it is modeled. Its refused twin is negative/subset/x029_subtraction.cpp.

verified int probe(int a, int b)
    expects (a >= 0 && a <= 1000 && b >= 0 && b <= 1000)
    ensures (result == a - b)
{
    return a - b;
}

int main() {
    return probe(5, 3) == 2 ? 0 : 1;
}
