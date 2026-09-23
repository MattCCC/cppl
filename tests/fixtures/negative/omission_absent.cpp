// A missing arm is never an intentional omission (SPEC.md CASE-004, CASE-005).
//
// The premise here is false, so `State::running` genuinely cannot occur, and
// `omit State::running by contradiction impossible;` would be accepted. It is
// not written. The case is simply absent, so the statement is non-exhaustive,
// even though the evidence that would discharge the case is in scope right
// there. The engine never scans the context to decide that a missing arm was
// meant: if it did, an accidental omission and a proved impossibility would be
// indistinguishable.
enum class State : int { idle = -1, running = 3 };

pure unsigned zero() {
    return 0u;
}

law absent_arm_under_a_false_premise(State s)
    expects (zero() == 1u)
    proves (Eq<State>(s, s));

proof absent_arm_under_a_false_premise_holds(State s)
    proves (absent_arm_under_a_false_premise(s))
{
    assume impossible : zero() == 1u;
    cases s {
        State::idle => {
            refl;
        }

        omit unnamed by contradiction impossible;
    }
}

int main() {
    return 0;
}
