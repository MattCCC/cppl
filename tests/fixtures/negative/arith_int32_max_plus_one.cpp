// SPEC: ARITH-006, DEFINEDBEHAVIOR-001
// `x <= INT_MAX` excludes nothing, so `x + 1` may be `INT_MAX + 1`. The twin
// with `x < INT_MAX` is int_below_max_plus_one in fixtures/signed_arithmetic.cpp.
#include <climits>

verified int int_at_max_plus_one(int x)
    expects (x <= INT_MAX)
    ensures (result == x + 1)
{
    return x + 1;
}

int main() {
    return int_at_max_plus_one(1) - 2;
}
