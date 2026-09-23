// Which evidence a statement names is never chosen by preference (SPEC.md
// PROOFSRC-005, PROOFSRC-006).
//
// `shared` is both a trusted law and a proof. Resolving the name to either would
// silently decide whether the proof below rests on an assumption, so the name
// is refused.
trusted law shared(unsigned x)
    proves (x + 0u == x);

proof shared(unsigned x, unsigned y)
    proves (x + y == y + x)
{
    refl;
}

proof uses_shared(unsigned z)
    proves (z + 0u == z)
{
    exact shared(z);
}

int main() {
    return 0;
}
