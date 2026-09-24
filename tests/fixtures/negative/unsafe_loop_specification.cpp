// SPEC: UNSAFE-003
// An unsafe block's statements are not a path the verifier walks, so a loop
// invariant written inside one would state what nothing checks.
verified unsigned loop_inside(unsigned n)
    ensures (result == 0u)
{
    unsafe {
        unsigned i = 0u;
        while (i < n)
            invariant (i <= n)
        {
            ++i;
        }
    }
    return 0u;
}

int main() {
    return static_cast<int>(loop_inside(1u));
}
