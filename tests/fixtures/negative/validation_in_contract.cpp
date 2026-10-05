// SPEC: RUNTIMECHECK-019, RUNTIMECHECK-013
// A contract states a proposition, and a validation expression is runtime
// code: a postcondition cannot ask the program to test its result.
type Positive = int where (self > 0);

verified int identity(int raw)
    ensures (validate<Positive>(result))
{
    return raw;
}

int main() {
    return identity(5) > 0 ? 0 : 1;
}
