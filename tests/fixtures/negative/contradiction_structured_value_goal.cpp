// A limit of the formal core, recorded as a rejection so that it cannot change
// silently (SPEC.md CASE-011).
//
// The premise is false, so the contradiction itself is established. What
// cannot be done is closing THIS goal from it: the goal equates two records,
// and the only rule that closes a goal from a false fact is linear arithmetic,
// which states equalities of integers and nothing else. No other rule derives
// an equality of structured values from `0 == 1`, and adding one would be a new
// kernel rule. So the proof is refused by name, as an unproven claim, rather
// than closed by anything this implementation would have to be trusted for.
struct Pair {
    int first;
    int second;
};

pure unsigned zero() {
    return 0u;
}

law pairs_under_a_false_premise(Pair p, Pair q)
    expects (zero() == 1u)
    proves (Eq<Pair>(p, q))
{
    assume impossible : zero() == 1u;
    contradiction impossible;
}

int main() {
    return 0;
}
