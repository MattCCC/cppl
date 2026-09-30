// SPEC: WORD-013
// Where the unit gives `validate` a meaning of its own, the expression is that
// ordinary C++, not a validation, and nothing is supposed from it: the
// crossing is refused.
template <class T>
bool validate(int value) {
    return value > 0;
}

type Positive = int where (self > 0);

verified int positive_or_one(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}

int main() {
    return positive_or_one(5);
}
