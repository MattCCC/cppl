// A Law discharged by evidence the author wrote, rather than by the compiler's
// own strategy (GRAMMAR.md 4, 5.1 - 5.3).

#include <iostream>

pure int identity(int x) {
    return x;
}

pure int twice_identity(int x) {
    return identity(identity(x));
}

law identity_returns_input(int x)
    proves (identity(x) == x);

proof identity_returns_input_holds(int x)
    proves (identity_returns_input(x))
{
    refl;
}

law identity_is_its_own_inverse(int x)
    proves (twice_identity(x) == x);

// The goal is not the one `identity_returns_input_holds` states, but the two
// are definitionally equal, so its evidence applies to this goal as well.
proof identity_is_its_own_inverse_holds(int x)
    proves (identity_is_its_own_inverse(x))
{
    apply identity_returns_input_holds;
}

law identity_returns_input_again(int x)
    proves (identity(x) == x);

// The same proposition, so the evidence is the goal's own.
proof identity_returns_input_again_holds(int x)
    proves (identity_returns_input_again(x))
{
    exact identity_returns_input_holds;
}

int main() {
    std::cout << twice_identity(41) << "\n";
    return 0;
}
