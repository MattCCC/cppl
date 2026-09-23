// The refused half of a matched pair (SPEC.md CASE-004, CASE-011).
//
// The accepted half, in `omitted_case.cpp`, states this same law with this same
// premise and evidence and omits `State::idle`, which the premise contradicts.
// This one omits `State::running`, which the premise agrees with. They differ
// only in which named case is omitted and which keeps its arm.
//
// Together they catch two different ways of getting omission wrong:
//
//   - checking the evidence without the omitted case's own discriminator. The
//     accepted half then fails, because its premise alone is satisfiable.
//   - asking whether the omitted case's goal follows, rather than whether the
//     case is contradictory. Here the goal does follow in the `State::running`
//     branch, where `s == State::running` holds, so that mistake accepts this
//     half.
//
// The surviving `State::idle` arm is itself impossible under the premise and
// is closed by an ordinary contradiction, so the omission is the only thing
// here that can fail.
enum class State : int { idle = -1, running = 3 };

law omission_uses_its_discriminator(State s)
    expects (s == State::running)
    proves (Eq<bool>(s == State::running, true));

proof omission_uses_its_discriminator_holds(State s)
    proves (omission_uses_its_discriminator(s))
{
    assume is_running : s == State::running;
    cases s {
        State::idle => {
            contradiction is_running;
        }

        omit State::running by contradiction is_running;

        omit unnamed by contradiction is_running;
    }
}

int main() {
    return 0;
}
