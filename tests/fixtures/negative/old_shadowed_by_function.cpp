// SPEC: WORD-001, WORD-007
// Inside a postcondition `old(x)` is the entry value of `x` (SPEC.md 11.4), and
// the formal form takes precedence over any C++ entity spelled `old` (SPEC.md
// 3.1). Entry values are not implemented, so the contract is refused. Read as
// a call to the function below, it claimed that `bump` leaves `x` unchanged,
// which is false, and was PROVEN assumption-free.
#include <cstdio>

pure unsigned old(unsigned v) {
    return v;
}

verified void bump(unsigned& x)
    expects (x < 10u)
    ensures (x == old(x))
{
    x = x + 1u;
}

int main() {
    unsigned value = 3u;
    bump(value);
    std::printf("after bump: %u (entry was 3)\n", value);
}
