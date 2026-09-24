// SPEC: LOOP-006
// A `continue` is a path to the next iteration like any other, and owes the
// descent: this one skips the step that makes the measure fall.
verified unsigned skips(unsigned n)
    ensures (result == 0u)
{
    unsigned i = n;
    while (i > 0u)
        invariant (i <= n)
        decreases (i)
    {
        if (i == 5u) {
            continue;
        }
        i = i - 1u;
    }
    return i;
}

int main() {
    return static_cast<int>(skips(3u));
}
