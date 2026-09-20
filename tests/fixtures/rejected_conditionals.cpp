// Conditional laws and proofs that must be refused.
//
// A precondition is supposed, never granted. None of these compiles, and each
// one fails for a reason that names the statement responsible.

#include <iostream>

pure unsigned identity(unsigned x) {
    return x;
}

pure unsigned add_one(unsigned x) {
    return x + 1u;
}

// The proposition named by `assume` is not the premise the law supposes.
law wrong_premise(unsigned x)
    expects(identity(x) == x)
    ensures(add_one(x) == x);

proof wrong_premise_holds(unsigned x)
    proves(wrong_premise(x))
{
    assume h : add_one(x) == x;
    exact h;
}

// A law that supposes nothing leaves nothing to assume.
law nothing_supposed(unsigned x)
    ensures(identity(x) == x);

proof nothing_supposed_holds(unsigned x)
    proves(nothing_supposed(x))
{
    assume h : identity(x) == x;
    exact h;
}

// Supposing a premise does not make a false conclusion true.
law false_under_a_premise(unsigned x)
    expects(identity(x) == x)
    ensures(add_one(x) == x);

proof false_under_a_premise_holds(unsigned x)
    proves(false_under_a_premise(x))
{
    refl;
}

// An application leaves the premise behind, and nothing in the body closes it.
law guarded(unsigned x)
    expects(identity(x) == x)
    ensures(add_one(x) == add_one(x));

proof guarded_holds(unsigned x)
    proves(guarded(x))
{
    refl;
}

law unguarded(unsigned x)
    ensures(add_one(x) == add_one(x));

proof unguarded_holds(unsigned x)
    proves(unguarded(x))
{
    apply guarded_holds(x);
}

// A premise offered evidence that does not establish it. The conclusion is
// true, the application is well formed, and only the kernel can tell that the
// premise handed to it was never proven.
law guarded_by_a_falsehood(unsigned x)
    expects(add_one(x) == x)
    ensures(identity(x) == identity(x));

proof guarded_by_a_falsehood_holds(unsigned x)
    proves(guarded_by_a_falsehood(x))
{
    refl;
}

law taken_unguarded(unsigned x)
    ensures(identity(x) == identity(x));

proof taken_unguarded_holds(unsigned x)
    proves(taken_unguarded(x))
{
    apply guarded_by_a_falsehood_holds(x);
    refl;
}

// Every goal is closed before the last statement is reached.
law already_closed(unsigned x)
    ensures(identity(x) == x);

proof already_closed_holds(unsigned x)
    proves(already_closed(x))
{
    refl;
    refl;
}

// Multiple Law preconditions are still refused; use one explicit conjunction.
law two_preconditions(unsigned x)
    expects(identity(x) == x)
    expects(add_one(x) == add_one(x))
    ensures(identity(x) == x);

int main() {
    std::cout << add_one(41u) << "\n";
    return 0;
}
