#include <cstdio>

enum class One : unsigned { one = 1u, alias = one };
enum class State : int { idle = -1, running = 3, unnamed = 8 };
using StateAlias = State;

// Case facts are independently checked and can rewrite expressions through the
// existing equality eliminator. The residual binder has the underlying type.
proof choose_one(One s)
    proves(Eq<bool>(s == One::one, static_cast<unsigned>(s) == 1u))
{
    cases s {
        One::alias => {
            assume here : s == One::one;
            rewrite here;
            refl;
        }
        unnamed(value) => {
            assume other : value != 1u;
            rewrite other;
            refl;
        }
    }
}

proof nested(StateAlias s, One t)
    proves(Eq<State>(s, s))
{
    cases s {
        State::running => { assume h : s == State::running; rewrite h; refl; }
        State::idle => {
            assume outer : s == State::idle;
            cases t {
                One::one => { rewrite outer; refl; }
                unnamed(value) => {
                    assume excluded : value != 1u;
                    rewrite outer;
                    refl;
                }
            }
        }
        State::unnamed => { refl; }
        unnamed(value) => {
            assume residual : value != static_cast<int>(State::idle) && value != 3 && value != 8;
            refl;
        }
    }
}

proof reused(One s)
    proves(Eq<bool>(s == One::one, static_cast<unsigned>(s) == 1u))
{
    exact choose_one(s);
}

enum class Empty : unsigned {};
proof no_enumerators(Empty s) proves(s == s) {
    cases s { unnamed(value) => { refl; } }
}

proof under_a_quantifier(One s) proves(forall(unsigned x) { x == x }) {
    cases s {
        One::one => { assume h : s == One::one; refl; }
        unnamed(value) => { assume h : value != 1u; refl; }
    }
}

law stable(State s) ensures(s == s);
proof stable_holds(State s) proves(stable(s)) {
    cases s {
        State::idle => { exact later(s); }
        State::running => { refl; }
        State::unnamed => { refl; }
        unnamed(value) => { refl; }
    }
}
proof later(State s) proves(s == s) { refl; }

// Every underlying value is a valid scoped-enum state, including unnamed ones.
int main() {
    auto s = static_cast<One>(37u);
    std::printf("%u %d %zu\n", static_cast<unsigned>(s), static_cast<int>(State::idle), sizeof(One));
}
