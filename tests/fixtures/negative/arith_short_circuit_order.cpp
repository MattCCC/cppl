// SPEC: ARITH-009, BOUNDARYEX-001, EDGECASE-030
// `x / y` stands left of `||`, so it runs before `y == 0` is tested and nothing
// protects it. The twin testing `y == 0` first is ratio_or_zero.
verified int ratio_tested_late(int x, int y)
    expects (x != -2147483647 - 1)
    ensures (result >= 0)
{
    if (x / y < 0 || y == 0)
        return 0;
    return x / y;
}

int main() {
    return ratio_tested_late(4, 2) - 2;
}
