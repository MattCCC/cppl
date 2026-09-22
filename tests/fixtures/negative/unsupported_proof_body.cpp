// A proof body written with a tactic this implementation does not have. It is
// refused rather than ignored: a proof statement that compiled to nothing would
// make the Law look discharged.

#include <iostream>

pure int identity(int x) {
    return x;
}

law identity_returns_input(int x)
    proves (identity(x) == x);

proof identity_returns_input_holds(int x) proves (identity_returns_input(x)) {
    let y = identity(x);
}

int main() {
    std::cout << identity(1) << "\n";
    return 0;
}
