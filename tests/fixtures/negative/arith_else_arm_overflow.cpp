// SPEC: ARITH-009, DEFINEDBEHAVIOR-001
// The else arm of `?:` runs wherever the condition fails, the least value
// included, where `x - 1` overflows. The initializer is one expression, so the
// arm owes its operation under the condition's outcome rather than on a route
// of its own. The twin whose precondition excludes the least value is
// away_from_zero_below in fixtures/signed_arithmetic.cpp.
verified int away_from_zero_below(int x)
    ensures (result <= x)
{
    int y = x >= 0 ? x : x - 1;
    return y;
}

int main() {
    return away_from_zero_below(0);
}
