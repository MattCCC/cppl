// Discharging a goal from a premise that cannot hold (GRAMMAR.md 5.6,
// SPEC.md CASE-004/005/011).
//
// `contradiction e;` is not an axiom. It is two steps the kernel checks: the
// named evidence and the premises standing where it is written are refuted into
// `False` by linear arithmetic, which the goal takes no part in, and the goal
// is then closed from `False` by falsity elimination, whatever its shape
// (FOUNDATIONS.md 26). The kernel states the constraints and checks the
// certificate itself, so a premise that is merely unproven, rather than
// contradictory, closes nothing.
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

// A goal with structure: a quantifier, a conjunction and an implication. It is
// closed as a whole, not piece by piece.
law compound_conclusion_under_a_false_premise(unsigned x)
    expects (zero() == 1u)
    proves ((forall(unsigned y) { y == x }) && (x == 2u -> x == 3u));

proof compound_conclusion_under_a_false_premise_holds(unsigned x)
    proves (compound_conclusion_under_a_false_premise(x))
{
    assume impossible : zero() == 1u;
    contradiction impossible;
}

// A goal no arithmetic states: two arbitrary records are equal. Nothing about
// the goal is looked at, so it closes exactly as an integer goal does. The
// refused half of this pair, with a satisfiable premise, is
// `fixtures/negative/contradiction_structured_goal_satisfiable.cpp`.
struct Pair {
    int first;
    int second;
};

law structured_conclusion_under_a_false_premise(Pair p, Pair q)
    expects (zero() == 1u)
    proves (Eq<Pair>(p, q))
{
    assume impossible : zero() == 1u;
    contradiction impossible;
}

// The same goal with no written proof: automation reaches it the same way, by
// refuting the premise into `False`.
law structured_conclusion_proven_automatically(Pair p, Pair q)
    expects (zero() == 1u)
    proves (Eq<Pair>(p, q));

// None of the proof syntax may reach the runtime.
int main() {
    std::printf("%u\n", add_one(zero()));
}
