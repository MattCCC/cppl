// Every proof-only declaration form beside the runtime code it talks about
// (SPEC.md ERASE-002, ERASE-005, Annex M).
//
// Erased, this unit must compile to exactly the code `proofs.reference.cpp`
// compiles to: that file is this one with the law and proof declarations and
// the `pure` specifiers removed by hand, as Annex M says they erase. Nothing a
// proof states may leave an instruction, a symbol or a datum behind.
#include <cstdio>

enum class State : int { idle = -1, running = 3 };

struct Point {
    int x;
    int y;
};

pure unsigned zero() {
    return 0u;
}

pure unsigned identity(unsigned x) {
    return x;
}

pure unsigned add(unsigned a, unsigned b) {
    return a + b;
}

// Proven by automation, with no written proof.
law identity_returns_input(unsigned x)
    proves (identity(x) == x);

// A law with its proof written inline.
law identity_of_zero()
    proves (identity(0u) == 0u)
{
    refl;
}

// An assumption: recorded in the trust report and never code.
trusted law add_is_stable(unsigned a, unsigned b)
    proves (add(identity(a), identity(b)) == add(a, b));

proof identity_returns_input_holds(unsigned x)
    proves (identity_returns_input(x))
{
    refl;
}

// `assume` and `rewrite`.
law identity_at_zero(unsigned x)
    expects (x == 0u)
    proves (identity(x) == 0u);

proof identity_at_zero_holds(unsigned x)
    proves (identity_at_zero(x))
{
    assume h : x == 0u;
    rewrite h;
    refl;
}

// `exact` and `apply` instantiate another proof.
law identity_of_seven()
    proves (identity(7u) == 7u);

proof identity_of_seven_holds()
    proves (identity_of_seven())
{
    exact identity_returns_input_holds(7u);
}

law add_at_two(unsigned b)
    proves (add(identity(2u), identity(b)) == add(2u, b));

proof add_at_two_holds(unsigned b)
    proves (add_at_two(b))
{
    apply add_is_stable(2u, b);
}

// `cases` over an enumeration, with a case omitted by contradiction.
law running_is_running(State s)
    expects (s == State::running)
    proves (Eq<bool>(s == State::running, true));

proof running_is_running_holds(State s)
    proves (running_is_running(s))
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

// `decompose` of a record.
proof point_fields(Point p)
    proves (Eq<bool>(true, true))
{
    decompose p {
        components(x, y) => {
            refl;
        }
    }
}

// `contradiction` closing a goal from a premise no value satisfies.
law anything_from_a_false_premise(unsigned x)
    expects (zero() == 1u)
    proves (x == zero());

proof anything_from_a_false_premise_holds(unsigned x)
    proves (anything_from_a_false_premise(x))
{
    assume impossible : zero() == 1u;
    contradiction impossible;
}

int main() {
    const State state = State::running;
    const Point point{3, 4};
    std::printf("%u %u %u %d %d %d\n", zero(), identity(7u), add(identity(2u), 5u), static_cast<int>(state), point.x,
                point.y);
}
