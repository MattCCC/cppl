// Discharging a goal from a premise that cannot hold (GRAMMAR.md 5.6,
// SPEC.md CASE-004/005/011).
//
// `contradiction e;` is not a new rule and not an axiom. The proposition
// language has no falsity constant, so the contradiction is carried by the
// existing linear-arithmetic rule in two steps: the named evidence and the
// premises standing where it is written are refuted into `0 == 1`, which the
// goal takes no part in, and the goal is then closed from that. The kernel
// states the constraints and checks both certificates itself, so a premise that
// is merely unproven, rather than contradictory, closes nothing.
#include <cstdio>

pure unsigned zero() {
    return 0u;
}

pure unsigned add_one(unsigned x) {
    return x + 1u;
}

// The premise `zero() == 1` is false. The conclusion is unrelated to it and is
// false on its own, so nothing but the premise being contradictory can close
// this goal. A law states the implication; it never claims the premise holds.
law unrelated_under_a_false_premise(unsigned x)
    expects (zero() == 1u)
    proves (x == zero());

proof unrelated_under_a_false_premise_holds(unsigned x)
    proves (unrelated_under_a_false_premise(x))
{
    assume impossible : zero() == 1u;
    contradiction impossible;
}

// The same shape at a different conclusion, to pin that what closed the goal
// was the premise and not something about the goal's own terms.
law another_conclusion_under_the_same_premise(unsigned x)
    expects (zero() == 1u)
    proves (add_one(x) == x);

proof another_conclusion_under_the_same_premise_holds(unsigned x)
    proves (another_conclusion_under_the_same_premise(x))
{
    assume impossible : zero() == 1u;
    contradiction impossible;
}

// A goal with structure: its quantifier, conjuncts and premise are introduced by
// the ordinary rules, and each equality left is closed from the contradiction.
// The premise stands outside the quantifier, so it is restated beneath it.
law structured_conclusion_under_a_false_premise(unsigned x)
    expects (zero() == 1u)
    proves ((forall(unsigned y) { y == x }) && (x == 2u -> x == 3u));

proof structured_conclusion_under_a_false_premise_holds(unsigned x)
    proves (structured_conclusion_under_a_false_premise(x))
{
    assume impossible : zero() == 1u;
    contradiction impossible;
}

// None of the proof syntax may reach the runtime.
int main() {
    std::printf("%u\n", add_one(zero()));
}
