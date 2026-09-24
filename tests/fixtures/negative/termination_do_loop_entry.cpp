// SPEC: LOOP-003
// A `do` loop's invariant holds before the body first runs, when nothing has
// checked the condition yet: at n == 0 it does not.
verified unsigned first_pass(unsigned n)
    ensures (result == 0u)
{
    unsigned i = n;
    do
        invariant (i > 0u)
        decreases (i)
    {
        i = i - 1u;
    } while (i > 0u);
    return i;
}

int main() {
    return static_cast<int>(first_pass(3u));
}
