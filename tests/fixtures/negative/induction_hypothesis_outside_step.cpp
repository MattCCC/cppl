// SPEC: INDUCT-001
// FOUNDATIONS.md 10: an induction hypothesis is local to its case. The name the
// successor arm binds for it is not in scope in the zero arm, whichever order
// the arms are written in, and the zero case supposes no hypothesis to name.
pure unsigned add(unsigned x, unsigned y) {
    return x + y;
}

law add_zero(unsigned x)
    proves (add(x, 0u) == x);

proof add_zero_by_induction(unsigned x)
    proves (add_zero(x))
{
    induction x {
        successor(pred) => {
            assume hypothesis : add(pred, 0u) == pred;
            refl;
        }

        zero => {
            exact hypothesis;
        }
    }
}

int main() {
    return static_cast<int>(add(0u, 0u));
}
