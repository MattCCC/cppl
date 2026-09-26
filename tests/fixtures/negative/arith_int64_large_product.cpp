// SPEC: ARITH-006, DEFINEDBEHAVIOR-001
// 2 * 4611686018427387904 is 2^63, one past `LLONG_MAX`. The twin bounding `x`
// by 4611686018427387903 is int64_doubled.
verified long long int64_doubled_too_far(long long x)
    expects (x >= -4611686018427387904LL && x <= 4611686018427387904LL)
    ensures (result == 2 * x)
{
    return x * 2;
}

int main() {
    return static_cast<int>(int64_doubled_too_far(0));
}
