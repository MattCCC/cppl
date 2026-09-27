// Ordinary C++: `contradiction.cpp` erased by hand as SPEC.md Annex M says it erases.
// Every Law and proof is gone whole, `pure` and `verified` leave their
// functions as written, and every clause is gone.
#include <cstdio>

unsigned zero() {
    return 0u;
}

unsigned add_one(unsigned x) {
    return x + 1u;
}

// A goal no arithmetic states: two arbitrary records are equal. Nothing about
// the goal is looked at, so it closes exactly as an integer goal does. The
// refused half of this pair, with a satisfiable premise, is
// `fixtures/negative/contradiction_structured_goal_satisfiable.cpp`.
struct Pair {
    int first;
    int second;
};

// None of the proof syntax may reach the runtime.
int main() {
    std::printf("%u\n", add_one(zero()));
}
