// SPEC: RUNTIMECHECK-019
// A loop invariant states a proposition, and a validation expression is
// runtime code: it cannot stand in one.
type Positive = int where (self > 0);

verified int countdown(int raw)
    expects (raw > 0)
    ensures (result > 0)
{
    int x = raw;
    while (x > 100)
        invariant (validate<Positive>(x))
    {
        x = x - 1;
    }
    return x;
}

int main() {
    return countdown(5) > 0 ? 0 : 1;
}
