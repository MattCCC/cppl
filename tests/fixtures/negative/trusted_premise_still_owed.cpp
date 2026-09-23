// A trusted law's premise is not assumed (SPEC.md PROOFSRC-005, TRUSTED-001).
//
// `device_bound` admits `x + 1u == 4u` only under `x == 3u`. Applying it leaves
// that premise as a goal like any other, and nothing here establishes it for an
// arbitrary `y`: trust in the law does not extend to the condition it states.
trusted law device_bound(unsigned x)
    expects (x == 3u)
    proves (x + 1u == 4u);

proof beyond_the_bound(unsigned y)
    proves (y + 1u == 4u)
{
    apply device_bound(y);
    refl;
}

int main() {
    return 0;
}
