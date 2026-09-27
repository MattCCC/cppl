// Ordinary C++: `omitted_case.cpp` erased by hand as SPEC.md Annex M says it erases.
// Every Law and proof is gone whole, `pure` and `verified` leave their
// functions as written, and every clause is gone.
#include <cstdio>

enum class One : unsigned { one = 1u };
enum class State : int { idle = -1, running = 3 };

unsigned zero() {
    return 0u;
}

// None of the proof syntax may reach the runtime.
int main() {
    std::printf("%u\n", zero());
}
