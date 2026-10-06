// SPEC: ERASE-018, RUNTIMECHECK-021
// A refinement a validation names lowers to its alias and its validator, which
// is more C++ than this declaration takes on its line. The function written
// after it on that line would move, and what it observes of its own position
// with it, so the unit is refused rather than compiled with the code moved.
#include <cstdio>

type Positive = int where (self > 0); unsigned after() { return __builtin_COLUMN(); }

verified int pick(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw)) {
        return raw;
    }
    return 1;
}

int main() {
    std::printf("%d %u\n", pick(3), after());
}
