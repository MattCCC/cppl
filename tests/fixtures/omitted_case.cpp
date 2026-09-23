// Accounting for a case without an arm (GRAMMAR.md 5.7, SPEC.md CASE-004
// clause 2, CASE-005, CASE-011).
//
// `omit label by contradiction e;` is the only way a case goes without an arm.
// The engine never decides that a missing arm was intentional: an absent case
// with no omission is non-exhaustive, exactly as before. What the omission adds
// is a written, checked claim that the case cannot occur, discharged by the same
// contradiction machinery a runtime path will use, under its own obligation
// origin.
#include <cstdio>

enum class One : unsigned { one = 1u };
enum class State : int { idle = -1, running = 3 };

pure unsigned zero() {
    return 0u;
}

// The premise `zero() == 1` is false, so every case of the subject is
// impossible under it and the residual one may be omitted. The law never claims
// the premise holds.
law residual_omitted(One s)
    expects (zero() == 1u)
    proves (Eq<One>(s, s));

proof residual_omitted_holds(One s)
    proves (residual_omitted(s))
{
    assume impossible : zero() == 1u;
    cases s {
        One::one => {
            refl;
        }

        omit unnamed by contradiction impossible;
    }
}

// A named case, rather than the residual one, and more than one omission in a
// single statement: nothing about the form is special to the tail.
law named_cases_omitted(State s)
    expects (zero() == 1u)
    proves (Eq<State>(s, s));

proof named_cases_omitted_holds(State s)
    proves (named_cases_omitted(s))
{
    assume impossible : zero() == 1u;
    cases s {
        State::idle => {
            refl;
        }

        omit State::running by contradiction impossible;

        omit unnamed by contradiction impossible;
    }
}

// The accepted half of the pair that pins CASE-011's context requirement.
//
// Nothing here is contradictory on its own: `s == State::running` is an
// ordinary satisfiable premise, unlike the false premises above. What rules
// `State::idle` out is that premise TOGETHER WITH `State::idle`'s own
// discriminator, which the omission supplies. It is the first case the
// partition splits on, so no other case's exclusion stands in its branch: its
// own discriminator is the only case fact there, and if it stopped reaching
// the evidence check this omission would fail.
//
// Its twin, `negative/rejected_omissions.cpp`, states the same law with the
// same premise and evidence and omits `State::running` instead: the case the
// premise agrees with. The two differ only in which named case is omitted and
// which keeps its arm.
law omission_uses_its_discriminator(State s)
    expects (s == State::running)
    proves (Eq<bool>(s == State::running, true));

proof omission_uses_its_discriminator_holds(State s)
    proves (omission_uses_its_discriminator(s))
{
    assume is_running : s == State::running;
    cases s {
        omit State::idle by contradiction is_running;

        State::running => {
            rewrite is_running;
            refl;
        }

        omit unnamed by contradiction is_running;
    }
}

// A pointer's `non_null` is its residual case, so its discriminator is the
// negation of `null`'s. The premise refutes it through that negation.
law null_pointer_is_null(int* pointer)
    expects (pointer == nullptr)
    proves (Eq<bool>(pointer == nullptr, true))
{
    assume is_null : pointer == nullptr;
    cases pointer {
        null => {
            rewrite is_null;
            refl;
        }

        omit non_null by contradiction is_null;
    }
}

// Omission underneath a quantifier the `cases` statement itself introduces:
// the omitted case's obligation is closed over the law's parameter and the
// goal's own binder, with the premise restated beneath both.
law omission_under_a_quantifier(State s)
    expects (s == State::idle)
    proves (forall(unsigned y) { y + 0u == y });

proof omission_under_a_quantifier_holds(State s)
    proves (omission_under_a_quantifier(s))
{
    assume is_idle : s == State::idle;
    cases s {
        State::idle => {
            refl;
        }

        omit State::running by contradiction is_idle;

        omit unnamed by contradiction is_idle;
    }
}

// None of the proof syntax may reach the runtime.
int main() {
    std::printf("%u\n", zero());
}
