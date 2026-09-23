// A trusted law establishes what it states and nothing stronger (SPEC.md
// PROOFSRC-005). Naming it does not make it evidence for another proposition.
pure unsigned zero() {
    return 0u;
}

trusted law sensor_identity(unsigned x)
    proves (x + zero() == x);

law stronger_than_assumed(unsigned x)
    proves (x + 1u == x)
{
    exact sensor_identity(x);
}

int main() {
    return 0;
}
