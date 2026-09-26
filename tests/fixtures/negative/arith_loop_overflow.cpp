// SPEC: ARITH-006, ARITH-009, LOOP-001
// One more iteration than the counter's bound allows: at `i == 2147483`,
// `total += 1000` would reach 2147484000. The twin bounding `n` by 2147483 is
// thousands.
verified int thousands_too_many(int n)
    expects (n >= 0 && n <= 2147484)
    ensures (result == n * 1000)
{
    int total = 0;
    for (int i = 0; i < n; ++i)
        invariant (0 <= i && i <= n && total == i * 1000)
    {
        total += 1000;
    }
    return total;
}

int main() {
    return thousands_too_many(0);
}
