// SPEC: ARITH-009, DEFINEDBEHAVIOR-001
// The then arm of `?:` runs wherever the condition holds, the greatest value
// included, where `x + 1` overflows. The initializer is one expression, so the
// arm owes its operation under the condition's outcome. The twin whose
// condition stops below the greatest value is saturated_successor in
// fixtures/signed_arithmetic.cpp.
verified int successor_from_zero(int x)
    ensures (result >= x)
{
    int y = x >= 0 ? x + 1 : x;
    return y;
}

int main() {
    return successor_from_zero(0) == 1 ? 0 : 1;
}
