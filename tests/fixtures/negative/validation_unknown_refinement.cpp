// SPEC: RUNTIMECHECK-018
// A validation tests a value against a refinement's predicate; an ordinary
// alias states none.
using Count = int;

verified int count_or_zero(int raw)
    ensures (result >= 0)
{
    if (validate<Count>(raw)) {
        return 0;
    }
    return 0;
}

int main() {
    return count_or_zero(5);
}
