// SPEC: RUNTIMECHECK-004, VERIFIED-014
// A verified call that may write the checked local by reference leaves a
// version the check says nothing of; only the callee's postcondition is known
// of it, and that does not keep it positive. Twin of `positive_or_one`.
type Positive = int where (self > 0);

verified void decrement(int& value)
    ensures (value <= 100)
{
    if (value > 100) {
        value = 100;
    }
}

verified int stale_after_call(int raw)
    ensures (result > 0)
{
    int value = raw;
    if (value <= 0) {
        return 1;
    }
    decrement(value);
    Positive p = value;
    return p;
}

int main() {
    return stale_after_call(5);
}
