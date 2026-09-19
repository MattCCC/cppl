// An `apply` whose conclusion has exactly the shape of the goal, but whose
// equality does not close it. Nothing before the kernel can tell the
// difference, and the kernel refuses it.

#include <iostream>

pure unsigned keep(unsigned x) {
    return x;
}

pure unsigned add_one(unsigned x) {
    return x + 1u;
}

law keep_returns_input(unsigned x)
    ensures(keep(x) == x);

proof keep_returns_input_holds(unsigned x)
    proves(keep_returns_input(x))
{
    refl;
}

law add_one_changes_nothing(unsigned x)
    ensures(add_one(x) == x);

proof add_one_changes_nothing_by_apply(unsigned x)
    proves(add_one_changes_nothing(x))
{
    apply keep_returns_input_holds;
}

int main() {
    std::cout << add_one(41u) << "\n";
    return 0;
}
