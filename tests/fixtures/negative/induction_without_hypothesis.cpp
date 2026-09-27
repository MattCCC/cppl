// SPEC: INDUCT-001, INDUCT-005, CORPUS-021
// The refused half of `constant_from_steps` in `fixtures/induction.cpp`. The
// successor case is closed without the induction hypothesis, and the premise
// at the predecessor alone relates the value at `pred + 1` only to the value at
// `pred`, never to the value at 0. Every case must prove the goal from what
// its principle supplies; this one does not.
pure unsigned scaled_square(unsigned k, unsigned m) {
    return k * m * m;
}

law constant_from_steps(unsigned k, unsigned n)
    expects (forall (unsigned m) { scaled_square(k, m + 1u) == scaled_square(k, m) })
    proves (scaled_square(k, n) == scaled_square(k, 0u));

proof constant_from_steps_by_induction(unsigned k, unsigned n)
    proves (constant_from_steps(k, n))
{
    induction n {
        zero => {
            refl;
        }

        successor(pred) => {
            assume steps : forall (unsigned m) { scaled_square(k, m + 1u) == scaled_square(k, m) };
            rewrite steps(pred);
            refl;
        }
    }
}

int main() {
    return static_cast<int>(scaled_square(0u, 0u));
}
