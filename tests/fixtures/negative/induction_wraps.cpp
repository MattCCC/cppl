// SPEC: INDUCT-003, INTERACT-026, EDGECASE-082
// `x + 1 != 0` holds at 0 and is preserved by every successor step but the
// last: at the maximum, `x + 1` wraps to 0. The successor case is proved under
// `pred < max` only, so it must show `pred + 2 != 0` for every `pred` below the
// maximum, which fails at `max - 1`. Offering the hypothesis as the successor
// case is refused, and so is leaving the case to automation.
law never_wraps(unsigned x)
    proves (x + 1u != 0u);

proof never_wraps_by_induction(unsigned x)
    proves (never_wraps(x))
{
    induction x {
        zero => {
            refl;
        }

        successor(pred) => {
            assume below : pred < 4294967295u;
            assume hypothesis : pred + 1u != 0u;
            exact hypothesis;
        }
    }
}

law never_wraps_again(unsigned x)
    proves (x + 1u != 0u);

proof never_wraps_again_automatically(unsigned x)
    proves (never_wraps_again(x))
{
    induction x;
}

int main() {
    return 0;
}
