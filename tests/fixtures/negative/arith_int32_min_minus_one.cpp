// SPEC: ARITH-006, DEFINEDBEHAVIOR-001
// `x >= INT_MIN` excludes nothing, so `x - 1` may be `INT_MIN - 1`. The twin
// with `x > INT_MIN` is int_above_min_minus_one.
#include <climits>

verified int int_at_min_minus_one(int x)
    expects (x >= INT_MIN)
    ensures (result == x - 1)
{
    return x - 1;
}

int main() {
    return int_at_min_minus_one(1);
}
