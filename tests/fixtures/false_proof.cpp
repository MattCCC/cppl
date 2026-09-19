// A written proof of a false Law. The proof is written correctly; the
// proposition is simply not true, and only the kernel can say so.

#include <iostream>

pure unsigned add_one(unsigned x) {
    return x + 1u;
}

law add_one_changes_nothing(unsigned x)
    ensures(add_one(x) == x);

proof add_one_changes_nothing_holds(unsigned x)
    proves(add_one_changes_nothing(x))
{
    refl;
}

int main() {
    std::cout << add_one(41u) << "\n";
    return 0;
}
