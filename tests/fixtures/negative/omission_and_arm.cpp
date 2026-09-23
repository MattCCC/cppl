// A case is accounted for by an arm or by an omission, never both (SPEC.md
// CASE-004). Writing both claims the case can occur and cannot, so the second
// account of it is refused as a duplicate, whichever order they are written in.
enum class State : int { idle = -1, running = 3 };

pure unsigned zero() {
    return 0u;
}

law armed_and_omitted(State s)
    expects (zero() == 1u)
    proves (Eq<State>(s, s));

proof armed_and_omitted_holds(State s)
    proves (armed_and_omitted(s))
{
    assume impossible : zero() == 1u;
    cases s {
        State::idle => {
            refl;
        }

        State::running => {
            refl;
        }

        omit State::running by contradiction impossible;

        omit unnamed by contradiction impossible;
    }
}

int main() {
    return 0;
}
