// SPEC: UNSAFE-005, INTERACT-018
// A loop counter an unsafe block writes is not known after the block, so the
// invariant that bounds it is not preserved, although the block only adds.
verified unsigned count(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
    {
        unsafe {
            i = i + 1u;
        }
    }
    return i;
}

int main() {
    return static_cast<int>(count(3u));
}
