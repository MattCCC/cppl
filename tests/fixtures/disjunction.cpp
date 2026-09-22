// Disjunction as a kernel proposition (SPEC.md 7.8, RFC 0011).
//
// A disjunction is established by one of its sides, and using one means proving
// the goal again under each side. Nothing here decides which side holds, and no
// statement of this program is evaluated to settle a proposition.

#include <cstdio>

pure unsigned identity(unsigned x) {
    return x;
}

// Either side establishes the disjunction. The other side is false at almost
// every value, so a rule that took the wrong side could not close these.
law from_the_left(unsigned x)
    proves (x == x || x == 1u);
law from_the_right(unsigned x)
    proves (x == 1u || x == x);
proof from_the_left_holds(unsigned x)
    proves (from_the_left(x))
{
    refl;
}

// Explicit equalities compose through `||` as they do through the other
// connectives.
proof formal_side(unsigned x)
    proves (Eq<unsigned>(identity(x), x) || Eq<unsigned>(x, 1u))
{
    refl;
}

// A disjunctive premise is used by cases: the conclusion has to follow from each
// side on its own.
law bounded_by_cases(unsigned x)
    expects (x == 0u || x == 1u)
    proves (x <= 1u);
law three_cases(unsigned x)
    expects (x == 0u || x == 1u || x == 2u)
    proves (x <= 2u);

// The same premise, with the conclusion reached by rewriting inside each case.
law identity_by_cases(unsigned x)
    expects (x == 0u || x == 0u)
    proves (identity(x) == 0u);

// `&&` binds tighter than `||`, which binds tighter than `->` (GRAMMAR.md 33).
// The looser readings of both are false at some value.
law conjunction_binds_tighter(unsigned x)
    proves (x == 0u && x != 0u || x == x);
law implication_is_looser(unsigned x)
    proves (x == 0u || x == 1u -> x <= 1u);

// Under a binder, and on either side of an implication.
law everywhere(unsigned x)
    proves (forall (unsigned y) { y == y || y == 1u });
law guarded(unsigned x)
    proves (x == 0u -> x == 0u || x == 1u);

// Contracts carry disjunctions too: this one is supposed by cases at the call
// site below and proven from each side here.
verified unsigned narrow(unsigned x)
    expects (x == 1u || x == 2u)
    ensures (result != 0u)
{
    return x;
}

verified unsigned call_narrow(unsigned x)
    expects (x == 1u)
    ensures (result != 0u)
{
    return narrow(x);
}

int main() {
    std::printf("%u %u\n", narrow(2u), call_narrow(1u));
}
