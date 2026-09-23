// A proof uses a trusted law only where a statement names it (SPEC.md
// PROOFSRC-005, CASE-011).
//
// `broken_counter` is named in two arms, so the proof rests on it. It is still
// not a premise standing in the third arm: `contradiction truth;` reasons from
// `truth` and the premises standing there, and `zero() == 0u` alone contradicts
// nothing. Were the assumption to leak into every arm, this would be accepted.
//
// Its twin, `only_where_named` in `fixtures/trust_closure.cpp`, names
// `broken_counter` in that arm instead and is accepted. The two differ only in
// the evidence that one contradiction names.
enum class State : int { idle = -1, running = 3 };

pure unsigned zero() {
    return 0u;
}

trusted law broken_counter()
    proves (zero() == 1u);

law only_where_named(State s)
    expects (zero() == 0u)
    proves (Eq<unsigned>(zero(), 1u))
{
    assume truth : zero() == 0u;
    cases s {
        State::idle => {
            exact broken_counter;
        }

        State::running => {
            contradiction truth;
        }

        omit unnamed by contradiction broken_counter;
    }
}

int main() {
    return 0;
}
