// SPEC: ARITH-009, DEFINEDBEHAVIOR-001
// Each arm of `?:` owes its own operation under its own condition, and here
// each arm's condition is the one that lets it overflow: `x + 1` where `x` may
// be `INT_MAX`. The twin that steps toward zero is toward_zero.
verified int away_from_zero(int x)
    ensures (result != 0 || x == 0)
{
    return x > 0 ? x + 1 : x;
}

int main() {
    return away_from_zero(0);
}
