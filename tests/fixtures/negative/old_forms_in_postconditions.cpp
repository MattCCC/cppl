// SPEC: WORD-001, WORD-007
// Every place a postcondition can write `old(...)` is the entry-value form,
// with a function named `old` in scope: after an implication, as an argument,
// under a quantifier, beside a conjunction, spaced from its parenthesis, and
// written by a macro, which is expanded before the form is read (SPEC.md 3.2).
// Each is refused where it is written; none is read as a call.
pure unsigned old(unsigned v) {
    return v;
}
pure unsigned keep(unsigned v) {
    return v;
}
#define ENTRY(e) old(e)

verified unsigned after_implication(unsigned x)
    ensures (x < 5u -> result == old(x))
{
    return x;
}

verified unsigned as_argument(unsigned x)
    ensures (result == keep(old(x)))
{
    return x;
}

verified unsigned under_quantifier(unsigned x)
    ensures (forall (unsigned y) { y == y && result == old(x) })
{
    return x;
}

verified unsigned spaced(unsigned x)
    ensures (result == x && result == old (x))
{
    return x;
}

verified unsigned through_macro(unsigned x)
    ensures (result == ENTRY(x))
{
    return x;
}

int main() {
    return static_cast<int>(after_implication(1u) + as_argument(2u) + under_quantifier(3u) + spaced(4u) +
                            through_macro(5u));
}
