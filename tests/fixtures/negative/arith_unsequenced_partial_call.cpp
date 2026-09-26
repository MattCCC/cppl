// SPEC: ARITH-009, CORRECT-002
// `stuck` never returns, so partial correctness lets it promise anything,
// here `result == 0 && result == 1`. C++ does not sequence `stuck(x)` before
// `x + 1` in `stuck(x) + (x + 1)`, so `x + 1` may run, and overflow, first:
// the promise of a call that has not returned must not excuse it. The twin
// adding after the call, `stuck(x) + 1`, is sequenced after it and is in
// fixtures/signed_arithmetic.cpp.
verified int stuck(int x)
    ensures (result == 0 && result == 1)
{
    for (;;)
        invariant (x == x)
    {
    }
}

verified int overflow_before_the_call(int x)
    ensures (result == result)
{
    return stuck(x) + (x + 1);
}

int main() {
    return 0;
}
