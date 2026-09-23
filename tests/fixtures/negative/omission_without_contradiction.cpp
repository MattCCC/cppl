// A premise that is merely satisfiable rules no case out (SPEC.md CASE-005).
//
// `zero() == 0u` is true, not contradictory. Nothing about it is inconsistent
// with the omitted case's discriminator, so it cannot account for the omission.
// If this were accepted, any case could be dropped under any true premise.
//
// The surviving arm proves the goal on its own, so the only thing that can fail
// here is the omission itself.
enum class State : int { idle = -1, running = 3 };

pure unsigned zero() {
    return 0u;
}

law omits_under_a_satisfiable_premise(State s)
    expects (zero() == 0u)
    proves (Eq<State>(s, s));

proof omits_under_a_satisfiable_premise_holds(State s)
    proves (omits_under_a_satisfiable_premise(s))
{
    assume satisfiable : zero() == 0u;
    cases s {
        State::idle => {
            refl;
        }

        omit State::running by contradiction satisfiable;

        unnamed(v) => {
            refl;
        }
    }
}

int main() {
    return 0;
}
