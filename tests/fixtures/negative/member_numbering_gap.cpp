// SPEC: STORAGE-002
//
// A member the representation cannot model is left out of the type's
// components, so the components no longer stand at the positions an access
// numbers members by. Tracking `s` anyway would put the place of `s.b` where an
// access to `s.a` resolves, and `a` would read as whatever `b` holds. The claim
// below is false whenever the two differ; it must not be proven.
//
// The accepted half of the pair, which returns `s.b` itself, is in
// `fixtures/untracked_members.cpp`.
struct S {
    float f;
    unsigned a;
    unsigned b;
};

verified unsigned read_a(S s)
    ensures (result == s.b)
{
    // The empty block makes the body track `s`, which is where the gap was
    // misread; it writes nothing.
    unsafe {
    }
    return s.a;
}

int main() {
    S s{0.0f, 1u, 2u};
    return static_cast<int>(read_a(s));
}
