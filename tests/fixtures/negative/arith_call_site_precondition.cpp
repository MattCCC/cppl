// SPEC: ARITH-009, DEFINEDBEHAVIOR-001
// `x + 1` in `successor` owes its obligation once, under the callee's
// precondition, and every call site owes that precondition in turn. The first
// call's argument is below `INT_MAX`; the second's is its result, which may be
// `INT_MAX`, so the second call site is refused. The twin with `expects (x <
// INT_MAX - 1)` is second_successor in fixtures/signed_arithmetic.cpp.
#include <climits>

verified int successor(int x)
    expects (x < INT_MAX)
    ensures (result == x + 1)
{
    return x + 1;
}

verified int second_successor(int x)
    expects (x < INT_MAX)
    ensures (result == x + 2)
{
    int y = successor(x);
    return successor(y);
}

int main() {
    return second_successor(1) - 3;
}
