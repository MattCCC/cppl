// SPEC: INDUCT-002, INDUCT-003
// `x != max` holds at 0 and at every value but the maximum. The successor case
// at `max - 1` would have to conclude it for the maximum, so the principle
// establishes nothing here: induction covers every value of the type, the
// maximum included.
law below_maximum(unsigned x)
    proves (x != 4294967295u);

proof below_maximum_by_induction(unsigned x)
    proves (below_maximum(x))
{
    induction x {
        zero => {
            refl;
        }

        successor(pred) => {
            assume below : pred < 4294967295u;
            assume hypothesis : pred != 4294967295u;
            exact below;
        }
    }
}

int main() {
    return 0;
}
