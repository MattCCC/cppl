// The refused half of a matched pair (SPEC.md CASE-011, CASE-015). The accepted
// half is `structured_conclusion_under_a_false_premise` in
// `fixtures/contradiction.cpp`: the same goal, an equality of two records, and
// the same proof. Only the premise differs, and here it is satisfiable.
//
// Falsity elimination closes a goal of any shape, so a structured goal is no
// harder to close than an integer one. That is exactly why the contradiction
// must be established first and on its own: nothing about the goal can help it,
// and a premise some value satisfies establishes no contradiction.
struct Pair {
    int first;
    int second;
};

pure unsigned zero() {
    return 0u;
}

law pairs_under_a_possible_premise(Pair p, Pair q, unsigned x)
    expects (x == zero())
    proves (Eq<Pair>(p, q))
{
    assume possible : x == zero();
    contradiction possible;
}

int main() {
    return 0;
}
