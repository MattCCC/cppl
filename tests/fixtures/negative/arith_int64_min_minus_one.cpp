// SPEC: ARITH-006, DEFINEDBEHAVIOR-001
// The 64-bit boundary: `x >= LLONG_MIN` excludes nothing. The twin with
// `x > LLONG_MIN` is int64_above_min_minus_one.
#include <climits>

verified long long int64_at_min_minus_one(long long x)
    expects (x >= LLONG_MIN)
    ensures (result == x - 1)
{
    return x - 1;
}

int main() {
    return static_cast<int>(int64_at_min_minus_one(1));
}
