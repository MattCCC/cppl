// A premise that is satisfiable closes nothing (SPEC.md CASE-005, CASE-011).
//
// `zero() == 0u` holds, so it contradicts nothing, and the goal is false for
// most `x`. If this were accepted, any goal could be proven under any premise.
pure unsigned zero() {
    return 0u;
}

law unrelated_under_a_true_premise(unsigned x)
    expects (zero() == 0u)
    proves (x == zero());

proof unrelated_under_a_true_premise_holds(unsigned x)
    proves (unrelated_under_a_true_premise(x))
{
    assume possible : zero() == 0u;
    contradiction possible;
}

int main() {
    return 0;
}
