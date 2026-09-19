// Laws stated under a precondition, and the proofs that discharge them.
//
// `expects(P) ensures(Q)` states `P -> Q`. It never claims that P holds: what a
// proof of such a law establishes is the conclusion under that supposition.

#include <iostream>

pure unsigned identity(unsigned x) {
    return x;
}

pure unsigned add_one(unsigned x) {
    return x + 1u;
}

// A conclusion that is its own premise. `add_one(x) == x` is false, so nothing
// the compiler can do establishes it; only the premise the law supposes closes
// this goal.
law increment_is_stable(unsigned x)
    expects(add_one(x) == x)
    ensures(add_one(x) == x);

proof increment_is_stable_holds(unsigned x)
    proves(increment_is_stable(x))
{
    assume premise : add_one(x) == x;
    exact premise;
}

// A conclusion that holds outright. The precondition is supposed and then left
// alone, which is a claim about the conclusion and not an appeal to the premise.
law identity_under_a_premise(unsigned x)
    expects(add_one(x) == x)
    ensures(identity(x) == x);

proof identity_under_a_premise_holds(unsigned x)
    proves(identity_under_a_premise(x))
{
    refl;
}

// A conditional law whose premise is definitionally true, so a proof can
// discharge it and keep the conclusion.
law guarded_increment(unsigned x)
    expects(identity(x) == x)
    ensures(add_one(x) == add_one(x));

proof guarded_increment_holds(unsigned x)
    proves(guarded_increment(x))
{
    refl;
}

// Modus ponens: the conclusion of the conditional law, with the premise it
// supposes discharged by the statement that follows the application.
law increment_is_itself(unsigned x)
    ensures(add_one(x) == add_one(x));

proof increment_is_itself_holds(unsigned x)
    proves(increment_is_itself(x))
{
    apply guarded_increment_holds(x);
    refl;
}

// The same, with the evidence instantiated at a literal rather than at the
// proof's own parameter.
law increment_is_itself_at_41()
    ensures(add_one(41u) == add_one(41u));

proof increment_is_itself_at_41_holds()
    proves(increment_is_itself_at_41())
{
    apply guarded_increment_holds(41u);
    refl;
}

// A premise carried through an application: assumed here, handed to a law that
// supposes it, and used to close the goal that application leaves behind.
law increment_is_stable_again(unsigned x)
    expects(add_one(x) == x)
    ensures(add_one(x) == x);

proof increment_is_stable_again_holds(unsigned x)
    proves(increment_is_stable_again(x))
{
    assume h : add_one(x) == x;
    apply increment_is_stable_holds(x);
    exact h;
}

int main() {
    std::cout << add_one(identity(40u)) << "\n";
    return 0;
}
