// Two proofs that each use the other. Neither ever reaches the kernel:
// circular evidence is not evidence, and an induction hypothesis comes only from
// `induction` and its principle, never from proofs naming each other (SPEC.md
// 21.4, PROOFSRC-007).

#include <iostream>

pure int identity(int x) {
    return x;
}

law identity_returns_input(int x)
    proves (identity(x) == x);

law identity_returns_input_again(int x)
    proves (identity(x) == x);

proof first(int x)
    proves (identity_returns_input(x))
{
    apply second;
}

proof second(int x)
    proves (identity_returns_input_again(x))
{
    apply first;
}

int main() {
    std::cout << identity(1) << "\n";
    return 0;
}
