// Two trusted laws may share a name as C++ overloads, but a proof statement
// names evidence by name alone (SPEC.md PROOFSRC-005). Which assumption the
// proof below would rest on is not decided by its argument's type: the name is
// refused.
trusted law overloaded(unsigned x)
    proves (x + 0u == x);

trusted law overloaded(unsigned long x)
    proves (x + 0ul == x);

proof uses_overloaded(unsigned z)
    proves (z + 0u == z)
{
    exact overloaded(z);
}

int main() {
    return 0;
}
