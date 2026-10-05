// SPEC: RUNTIMECHECK-020
// A validation tests the predicate one declaration states. Percentage's base
// type is itself a refinement, whose predicate a validation of Percentage
// would not test, so it is refused rather than read as membership.
type NonNegative = int where (self >= 0);
type Percentage = NonNegative where (self <= 100);

verified int percentage_or_zero(int raw)
    ensures (result >= 0 && result <= 100)
{
    if (validate<Percentage>(raw)) {
        Percentage p = raw;
        return p;
    }
    return 0;
}

int main() {
    return percentage_or_zero(5);
}
