// SPEC: FORALL-001, REFINE-003
// A binder of a refinement type ranges over the values of that type, not over
// its whole base type (SPEC.md 8, 8.1). Its predicate is a fact the type
// supplies, so what holds of every value of the refinement is provable, and
// instantiating a quantifier over it at a term owes that the term is a value
// of it. Laws, proof declarations and contracts all quantify this way; so do a
// law's and a proof's own parameters (SPEC.md 8).
//
// The refused twin, negative/twins/refined_quantifiers.cpp, instantiates at a
// term not shown to be a value of the refinement.

#include <cstdio>

type Small = unsigned where (self < 10u);

pure unsigned twice(unsigned x) {
    return x + x;
}

// True of every Small, false of 10u: only the refinement's values are claimed.
law twice_small_bounded(unsigned n)
    proves (forall (Small s) { twice(s) < 20u });

// Several binders, refined and not, each ranging over its own type.
law mixed_binders(unsigned n)
    proves (forall (Small a, unsigned b, Small c) { a + c < 20u });

// A refined parameter ranges over its type as a binder does, by automation and
// by a written proof that names the fact its type supplies.
law small_below(Small s)
    proves (s < 10u);
law small_written(Small s)
    proves (s < 10u)
{
    assume small : s < 10u;
    exact small;
}

// Instantiating a refined binder at a term owes the term's membership, here
// discharged by the premise that establishes it.
law instance_at_member(unsigned n)
    expects (n < 10u)
    proves ((forall (Small s) { twice(s) < 20u }) -> twice(n) < 20u)
{
    assume small : n < 10u;
    assume bounded : forall(Small s){twice(s) < 20u};
    apply bounded(n);
    exact small;
}

// At a literal, the membership owed is closed and reduces to true.
proof at_five()
    proves ((forall (Small s) { twice(s) < 20u }) -> twice(5u) < 20u)
{
    assume bounded : forall(Small s){twice(s) < 20u};
    apply bounded(5u);
    refl;
}

// A trusted law over a refined parameter is assumed only of the refinement's
// values, so a use of it owes the argument's membership too.
trusted law small_twice_trusted(Small s)
    proves (twice(s) < 20u);
proof use_trusted(unsigned n)
    proves (n < 10u -> twice(n) < 20u)
{
    assume small : n < 10u;
    apply small_twice_trusted(n);
    exact small;
}

// A contract states a quantifier over a refinement in the same way.
verified unsigned keep(unsigned x)
    ensures (result == x && forall (Small s) { s < 10u })
{
    return x;
}

int main() {
    std::printf("%u %u\n", keep(3u), twice(4u));
}
