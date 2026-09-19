// Rewrites that must be refused.
//
// `rewrite` transforms a goal with an equality that has already been
// established. It never asserts one, and the goal it leaves is an obligation
// like any other.

#include <iostream>

pure unsigned identity(unsigned x) {
    return x;
}

pure unsigned add_one(unsigned x) {
    return x + 1u;
}

// The equality holds, and the term it rewrites does not appear in the goal.
// That is an error, not a rewrite that does nothing.
law absent_from_the_goal(unsigned x)
    expects(x == 0u)
    ensures(add_one(1u) == add_one(1u));

proof absent_from_the_goal_holds(unsigned x)
    proves(absent_from_the_goal(x))
{
    assume h : x == 0u;
    rewrite h;
    refl;
}

// Evidence that is not an equality has nothing to rewrite with.
law conditional(unsigned x)
    expects(identity(x) == x)
    ensures(add_one(x) == add_one(x));

proof conditional_holds(unsigned x)
    proves(conditional(x))
{
    refl;
}

law rewritten_by_an_implication(unsigned x)
    ensures(add_one(x) == add_one(x));

proof rewritten_by_an_implication_holds(unsigned x)
    proves(rewritten_by_an_implication(x))
{
    rewrite conditional_holds(x);
    refl;
}

// The rewrite is legitimate and what it leaves is still false. Only the kernel
// can say so, and it is the kernel that says so.
law false_after_rewriting(unsigned x)
    expects(x == 0u)
    ensures(identity(x) == 41u);

proof false_after_rewriting_holds(unsigned x)
    proves(false_after_rewriting(x))
{
    assume h : x == 0u;
    rewrite h;
    refl;
}

// A rewrite leaves a goal, and nothing in the body closes it.
law nothing_closes_it(unsigned x)
    expects(x == 0u)
    ensures(identity(x) == 0u);

proof nothing_closes_it_holds(unsigned x)
    proves(nothing_closes_it(x))
{
    assume h : x == 0u;
    rewrite h;
}

int main() {
    std::cout << add_one(41u) << "\n";
    return 0;
}
