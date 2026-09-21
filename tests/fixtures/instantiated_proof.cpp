// Evidence instantiated at a term: universally quantified proofs used at
// particular values, at terms built from the using proof's own parameters, and
// one quantifier at a time (SPEC.md 8, GRAMMAR.md 5.2 - 5.3).

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

// The general statement is not this claim, and `exact identity_general;` would
// be refused. Instantiated at 41 it is exactly this claim.
law identity_of_41()
    proves (identity(41u) == 41u);

proof identity_at_41()
    proves (identity_of_41())
{
    exact identity_general(41u);
}

// Instantiation at a term mentioning the instantiating proof's own parameter.
law identity_of_successor(unsigned x)
    proves (identity(add(x, 1u)) == add(x, 1u));

proof identity_of_successor_holds(unsigned x)
    proves (identity_of_successor(x))
{
    exact identity_general(add(x, 1u));
}

law both_identities(unsigned a, unsigned b)
    proves (add(identity(a), identity(b)) == add(a, b));

proof both_identities_hold(unsigned a, unsigned b)
    proves (both_identities(a, b))
{
    refl;
}

// Two quantifiers, eliminated one argument at a time.
law both_at_seven_and_three()
    proves (add(identity(7u), identity(3u)) == add(7u, 3u));

proof both_at_seven_and_three_hold()
    proves (both_at_seven_and_three())
{
    exact both_identities_hold(7u, 3u);
}

// Only the first quantifier is eliminated; the second still stands.
law both_at_seven(unsigned b)
    proves (add(identity(7u), identity(b)) == add(7u, b));

proof both_at_seven_hold(unsigned b)
    proves (both_at_seven(b))
{
    exact both_identities_hold(7u);
}

law both_at_two(unsigned b)
    proves (add(identity(2u), identity(b)) == add(2u, b));

proof both_at_two_hold(unsigned b)
    proves (both_at_two(b))
{
    apply both_identities_hold(2u, b);
}

int main() {
    std::cout << add(identity(41u), 0u) << "\n";
    return 0;
}
