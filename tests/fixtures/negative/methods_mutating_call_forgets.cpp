// SPEC: CLASS-011
// A mutating call may write every member of its object, and its postcondition
// is all that is known of them afterwards. `zero_a` states `a == 0u` and
// nothing about `b`, so what `b` held before the call is not known after it.
// The accepted half claims only what `zero_a` states: `mutating_call` in
// `fixtures/verified_methods.cpp`.
struct Pair {
    unsigned a;
    unsigned b;

    verified void zero_a()
        ensures (a == 0u)
    {
        a = 0u;
    }
};

verified unsigned mutating_call(unsigned x)
    ensures (result == 7u)
{
    Pair p{x, 7u};
    p.zero_a();
    return p.b;
}

int main() {
    return static_cast<int>(mutating_call(1u));
}
