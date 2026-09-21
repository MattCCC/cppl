// Equalities used to transform a goal.
//
// Until a proof could rewrite with an equality, a premise could only be named:
// it closed a goal that already was that premise, and nothing else. These laws
// need the premise to be used, and none of them is provable without `rewrite`.

#include <iostream>

pure unsigned identity(unsigned x) {
    return x;
}

// The premise has to reach inside the conclusion. `identity(x)` reduces to `x`
// and no further, so nothing but the equality closes the gap to `0`.
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

// The same equality, used where the conclusion states it the other way round.
// Symmetry needs no rule of its own: it is this rule at the context `0 == -`.
law zero_is_the_input(unsigned x)
    expects (x == 0u)
    proves (0u == x);

proof zero_is_the_input_holds(unsigned x)
    proves (zero_is_the_input(x))
{
    assume h : x == 0u;
    rewrite h;
    refl;
}

// A law proven once and then used as a rewrite rule. Both occurrences of
// `identity(x)` in the goal are transformed together.
law identity_returns_input(unsigned x)
    proves (identity(x) == x);

proof identity_returns_input_holds(unsigned x)
    proves (identity_returns_input(x))
{
    refl;
}

law nested_identity_is_input(unsigned x)
    proves (identity(identity(x)) == identity(x));

proof nested_identity_is_input_holds(unsigned x)
    proves (nested_identity_is_input(x))
{
    rewrite identity_returns_input_holds(x);
    refl;
}

// Two rewrites in sequence: the law first, then the premise.
law identity_at_zero_again(unsigned x)
    expects (x == 0u)
    proves (identity(x) == 0u);

proof identity_at_zero_again_holds(unsigned x)
    proves (identity_at_zero_again(x))
{
    assume h : x == 0u;
    rewrite identity_returns_input_holds(x);
    rewrite h;
    refl;
}

int main() {
    std::cout << identity(41u) << "\n";
    return 0;
}
