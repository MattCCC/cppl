// SPEC: RUNTIMECHECK-020
// A predicate stated as a formal proposition is not a C++ expression the
// program can evaluate.
type Bounded = int where (self > 100 -> self < 200);

verified int bounded_or_zero(int raw)
    ensures (result >= 0)
{
    if (validate<Bounded>(raw)) {
        return 1;
    }
    return 0;
}

int main() {
    return bounded_or_zero(5);
}
