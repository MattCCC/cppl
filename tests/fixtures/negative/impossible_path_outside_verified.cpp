// A claim that a path cannot occur is an obligation of a verified function
// (SPEC.md VERIFIED-023). In an ordinary function nothing would check it, so it
// is refused rather than erased unchecked.
pure unsigned zero() {
    return 0u;
}

proof nothing() proves (zero() == 0u)
{
    refl;
}

unsigned ordinary(unsigned x) {
    if (x > x) {
        contradiction nothing;
    }
    return x;
}

int main() {
    return 0;
}
