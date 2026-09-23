#include <cstdio>

enum class One : unsigned { one = 1u, alias = one };
enum class State : int { idle = -1, running = 3, unnamed = 8 };
using StateAlias = State;

// Case facts are independently checked and can rewrite expressions through the
// existing equality eliminator. The residual binder has the underlying type.
proof choose_one(One s)
    proves (Eq<bool>(s == One::one, static_cast<unsigned>(s) == 1u))
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
    proves (Eq<State>(s, s))
{
    cases s {
        State::running => {
            assume h : s == State::running;
            rewrite h;
            refl;
        }

        State::idle => {
            assume outer : s == State::idle;
            cases t {
                One::one => {
                    rewrite outer;
                    refl;
                }

                unnamed(value) => {
                    assume excluded : value != 1u;
                    rewrite outer;
                    refl;
                }
            }
        }

        State::unnamed => {
            refl;
        }

        unnamed(value) => {
            assume residual : value != static_cast<int>(State::idle) && value != 3 && value != 8;
            refl;
        }
    }
}

proof reused(One s)
    proves (Eq<bool>(s == One::one, static_cast<unsigned>(s) == 1u))
{
    exact choose_one(s);
}

enum class Empty : unsigned {};
proof no_enumerators(Empty s)
    proves (s == s)
{
    cases s {
        unnamed(value) => {
            refl;
        }
    }
}

proof under_a_quantifier(One s)
    proves (forall(unsigned x) { x == x })
{
    cases s {
        One::one => {
            assume h : s == One::one;
            refl;
        }

        unnamed(value) => {
            assume h : value != 1u;
            refl;
        }
    }
}

law stable(State s)
    proves (s == s);
proof stable_holds(State s)
    proves (stable(s))
{
    cases s {
        State::idle => {
            exact later(s);
        }

        State::running => {
            refl;
        }

        State::unnamed => {
            refl;
        }

        unnamed(value) => {
            refl;
        }
    }
}
proof later(State s)
    proves (s == s)
{
    refl;
}

// SPEC: CASE-002
// The top bit of an unsigned underlying type is part of the value, not a sign.
// Each enumerator, each discriminator and each residual premise carries the value
// C++ gives it, which is the value a literal of the underlying type denotes. The
// refused half of this matched pair is
// `fixtures/negative/enum_top_bit_claimed_false.cpp`.
enum class Top : unsigned { top = 0xFFFFFFFFu, low = 0u };

proof top_of_unsigned(Top s)
    proves (Eq<bool>(s == Top::top, static_cast<unsigned>(s) == 4294967295u))
{
    cases s {
        Top::top => {
            assume here : s == Top::top;
            rewrite here;
            refl;
        }

        Top::low => {
            assume here : s == Top::low;
            rewrite here;
            refl;
        }

        unnamed(value) => {
            assume residual : value != 4294967295u && value != 0u;
            refl;
        }
    }
}

// At full width the pattern and the value coincide; this pins that the fix for
// narrower types did not disturb it.
enum class Wide : unsigned long long { top = 18446744073709551615ull, low = 0ull };

proof top_of_unsigned_long_long(Wide s)
    proves (Eq<bool>(s == Wide::top, static_cast<unsigned long long>(s) == 18446744073709551615ull))
{
    cases s {
        Wide::top => {
            assume here : s == Wide::top;
            rewrite here;
            refl;
        }

        Wide::low => {
            assume here : s == Wide::low;
            rewrite here;
            refl;
        }

        unnamed(value) => {
            assume residual : value != 18446744073709551615ull && value != 0ull;
            refl;
        }
    }
}

// SPEC: CASE-002
// A member enumeration of a class template is a distinct enumeration per
// instantiation, and C++ names its enumerators only through the template's
// arguments. Labels are id-expressions, so they are written the same way. The
// refused half of this matched pair, a label from another instantiation, is
// `fixtures/negative/enum_label_of_another_instantiation.cpp`.
template <typename T> struct Machine {
    enum class Mode : unsigned { off = 0u, on = 1u };
};

proof member_enum_of_template(Machine<int>::Mode m)
    proves (Eq<bool>(m == Machine<int>::Mode::on, static_cast<unsigned>(m) == 1u))
{
    cases m {
        Machine<int>::Mode::off => {
            assume here : m == Machine<int>::Mode::off;
            rewrite here;
            refl;
        }

        Machine<int>::Mode::on => {
            assume here : m == Machine<int>::Mode::on;
            rewrite here;
            refl;
        }

        unnamed(value) => {
            assume residual : value != 0u && value != 1u;
            refl;
        }
    }
}

// An alias template denotes its argument, so the subject is the same
// enumeration however it is spelled.
template <typename T> using Same = T;

proof enum_through_alias_template(Same<Machine<Same<long>>::Mode> m)
    proves (Eq<bool>(m == Machine<long>::Mode::off, static_cast<unsigned>(m) == 0u))
{
    cases m {
        Machine<long>::Mode::off => {
            assume here : m == Machine<long>::Mode::off;
            rewrite here;
            refl;
        }

        Machine<long>::Mode::on => {
            assume here : m == Machine<long>::Mode::on;
            rewrite here;
            refl;
        }

        unnamed(value) => {
            assume residual : value != 0u && value != 1u;
            refl;
        }
    }
}

// Every underlying value is a valid scoped-enum state, including unnamed ones.
int main() {
    auto s = static_cast<One>(37u);
    std::printf("%u %d %zu\n", static_cast<unsigned>(s), static_cast<int>(State::idle), sizeof(One));
}
