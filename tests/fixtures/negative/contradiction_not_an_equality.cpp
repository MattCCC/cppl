// Evidence that is not an equality states no contradiction (SPEC.md CASE-011).
//
// The named proof is in scope and well formed; it establishes a quantified
// proposition. It is refused for what it establishes rather than for not being
// found.
pure unsigned zero() {
    return 0u;
}

proof quantified_evidence()
    proves (forall(unsigned y) { y == y })
{
    refl;
}

law unrelated_under_a_false_premise(unsigned x)
    expects (zero() == 1u)
    proves (x == zero());

proof unrelated_under_a_false_premise_holds(unsigned x)
    proves (unrelated_under_a_false_premise(x))
{
    assume impossible : zero() == 1u;
    contradiction quantified_evidence;
}

int main() {
    return 0;
}
