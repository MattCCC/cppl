// Circular proofs are refused whatever else they use (SPEC.md PROOFSRC-007,
// TRUST.md TCB-PROV-005). Naming a trusted law does not give a cycle of proofs
// a place to start: neither proof below has evidence, so neither is proven
// relative to anything.
pure unsigned zero() {
    return 0u;
}

trusted law sensor_identity(unsigned x)
    proves (x + zero() == x);

proof going_round(unsigned y)
    proves (y + zero() == y)
{
    rewrite sensor_identity(y);
    exact coming_back(y);
}

proof coming_back(unsigned y)
    proves (y == y)
{
    rewrite going_round(y);
    refl;
}

int main() {
    return 0;
}
