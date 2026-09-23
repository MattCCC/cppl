// A claim that a runtime path cannot occur names a proof declaration as its
// evidence (SPEC.md VERIFIED-045, TRUSTED-006). A trusted law is not one, so it
// is refused here rather than resolved some other way.
//
// Naming a proof that names the trusted law is accepted, and the claim, the
// function's contract and every caller's contract then rest on that law: see
// `never_seven` in `fixtures/trust_closure.cpp`.
pure unsigned zero() {
    return 0u;
}

trusted law broken_counter()
    proves (zero() == 1u);

verified unsigned never_seven(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    if (x == 7u) {
        contradiction broken_counter;
    }
    return x;
}

int main() {
    return 0;
}
