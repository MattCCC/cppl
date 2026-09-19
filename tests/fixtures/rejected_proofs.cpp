// Written proofs that must be refused before any evidence reaches the kernel:
// evidence for the wrong proposition, a conclusion that cannot be applied to
// the goal, and a proof that names something that was never declared.

#include <iostream>

pure int identity(int x) {
    return x;
}

pure unsigned add_one(unsigned x) {
    return x + 1u;
}

law identity_returns_input(int x)
    ensures(identity(x) == x);

proof identity_returns_input_holds(int x)
    proves(identity_returns_input(x))
{
    refl;
}

law add_one_increments(unsigned x)
    ensures(add_one(x) == x + 1u);

// `exact` demands the goal's own proposition, and this is a different one.
proof add_one_increments_by_exact(unsigned x)
    proves(add_one_increments(x))
{
    exact identity_returns_input_holds;
}

law add_one_is_add_one(unsigned x)
    ensures(add_one(x) == add_one(x));

// The conclusion quantifies over a different type, so it cannot be applied.
proof add_one_is_add_one_by_apply(unsigned x)
    proves(add_one_is_add_one(x))
{
    apply identity_returns_input_holds;
}

law identity_is_identity(int x)
    ensures(identity(x) == identity(x));

// A proof quantifies over exactly what its law quantifies over.
proof identity_is_identity_holds(int x, int y)
    proves(identity_is_identity(x))
{
    refl;
}

law identity_returns_input_again(int x)
    ensures(identity(x) == x);

proof identity_returns_input_again_holds(int x)
    proves(identity_returns_input_again(x))
{
    exact no_such_proof;
}

int main() {
    std::cout << identity(1) << "\n";
    return 0;
}
