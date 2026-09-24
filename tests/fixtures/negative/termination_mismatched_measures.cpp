// SPEC: TERMINATION-007
// The functions of one recursion share one ranking: measures of different
// lengths cannot be compared component by component.
unsigned right(unsigned m, unsigned n);

verified unsigned left(unsigned m, unsigned n)
    ensures (result == 0u)
    decreases (m, n)
{
    if (m == 0u) {
        return 0u;
    }
    return right(m - 1u, n);
}

verified unsigned right(unsigned m, unsigned n)
    ensures (result == 0u)
    decreases (m)
{
    if (m == 0u) {
        return 0u;
    }
    return left(m - 1u, n);
}

int main() {
    return static_cast<int>(left(2u, 1u));
}
