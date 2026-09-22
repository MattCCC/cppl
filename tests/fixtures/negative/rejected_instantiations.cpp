// Instantiations of written evidence that must be refused: at a term of the
// wrong type, at more arguments than the evidence quantifies over, at the wrong
// value, and at too few arguments to reach the claim.

#include <iostream>

pure unsigned identity(unsigned x) {
    return x;
}

pure unsigned add(unsigned a, unsigned b) {
    return a + b;
}

law identity_returns_input(unsigned x)
    proves (identity(x) == x);

proof identity_general(unsigned x)
    proves (identity_returns_input(x))
{
    refl;
}

law both_identities(unsigned a, unsigned b)
    proves (add(identity(a), identity(b)) == add(a, b));

proof both_identities_hold(unsigned a, unsigned b)
    proves (both_identities(a, b))
{
    refl;
}

// The general statement is not this claim. Only instantiating it makes it one,
// which is what `exact identity_general(41u)` does elsewhere.
law at_41_uninstantiated()
    proves (identity(41u) == 41u);

proof at_41_uninstantiated_holds()
    proves (at_41_uninstantiated())
{
    exact identity_general;
}

law at_41_by_wrong_type()
    proves (identity(41u) == 41u);

proof at_41_by_wrong_type_holds()
    proves (at_41_by_wrong_type())
{
    exact identity_general(41);
}

law at_41_by_too_many()
    proves (identity(41u) == 41u);

proof at_41_by_too_many_holds()
    proves (at_41_by_too_many())
{
    exact identity_general(41u, 7u);
}

law at_42_by_wrong_value()
    proves (identity(42u) == 42u);

proof at_42_by_wrong_value_holds()
    proves (at_42_by_wrong_value())
{
    exact identity_general(41u);
}

// One argument short: what the elimination leaves is still quantified, and the
// claim is not.
law at_seven_and_three()
    proves (add(identity(7u), identity(3u)) == add(7u, 3u));

proof at_seven_and_three_holds()
    proves (at_seven_and_three())
{
    exact both_identities_hold(7u);
}

// The instantiated conclusion has exactly the claim's shape, so nothing before
// the kernel can tell that it establishes a different equality.
law at_42_by_apply()
    proves (identity(42u) == 42u);

proof at_42_by_apply_holds()
    proves (at_42_by_apply())
{
    apply identity_general(41u);
}

int main() {
    std::cout << identity(1u) << "\n";
    return 0;
}
