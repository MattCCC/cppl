// SPEC: UNSAFE-003, UNSAFE-005
//
// A parameter this body does not track has one value throughout it, so an
// unsafe block that may change it is refused rather than followed by a stale
// value. Writing a member changes the object it belongs to: `s.x = 5u` changes
// `s` as surely as `s = t` would. The claim below is false after the block, and
// must not be proven. The private member is what leaves `s` untracked.
//
// The accepted half of the pair, whose block only reads `s.x`, is in
// `fixtures/untracked_members.cpp`.
struct S {
    unsigned x;
    S(unsigned a, unsigned b) : x(a), y(b) {}

  private:
    unsigned y;
};

verified unsigned keep(S s)
    ensures (result == s.x)
{
    unsafe {
        s.x = 5u;
    }
    return s.x;
}

int main() {
    S s{1u, 2u};
    return static_cast<int>(keep(s));
}
