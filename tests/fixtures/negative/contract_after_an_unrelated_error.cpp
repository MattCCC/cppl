// A contract is read whatever was reported before it. The first function's
// postcondition is refused, and that refusal must not also swallow the second
// function's contract, which is false and must be reported as false rather than
// dropped without a word.
verified unsigned halves(unsigned x)
    ensures (result == x >> 1u)
{
    return x;
}

verified unsigned claims_zero(unsigned x)
    expects (x > 0u)
    ensures (result == 0u)
{
    return x;
}

int main() {
    return 0;
}
