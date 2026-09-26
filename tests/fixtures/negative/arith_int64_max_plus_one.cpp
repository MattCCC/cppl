// SPEC: ARITH-006, DEFINEDBEHAVIOR-001
// The 64-bit boundary: `x <= LLONG_MAX` excludes nothing. The twin with
// `x < LLONG_MAX` is int64_below_max_plus_one.
#include <climits>

verified long long int64_at_max_plus_one(long long x)
    expects (x <= LLONG_MAX)
    ensures (result == x + 1)
{
    return x + 1;
}

int main() {
    return static_cast<int>(int64_at_max_plus_one(1)) - 2;
}
