// SPEC: CORRECT-001, CORRECT-002, CORRECT-003, CORRECT-005, CORRECT-006
// Contracts proven for partial correctness only, each beside a total counterpart.
// e2e/partial_correctness.sh checks that compiling this warns at each partial
// contract, naming every reason it is partial, at no total one, and that the
// warning changes neither the verdict nor the exit status.

unsafe unsigned read_sample();

// Partial: its loop states no measure, and never ends when n > 0.
verified unsigned loop_nodec(unsigned n)
    ensures (result >= n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
    {
        i = i + 0u;
    }
    return i;
}

// Total counterpart: the same loop, with a measure each iteration descends.
verified unsigned loop_dec(unsigned n)
    ensures (result >= n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
        decreases (n - i)
    {
        i = i + 1u;
    }
    return i;
}

// Partial: its own body has no loop, and the callee's contract is partial.
verified unsigned calls_partial(unsigned n)
    ensures (result >= n)
{
    return loop_nodec(n);
}

// Total counterpart: the same call, to the total callee.
verified unsigned calls_total(unsigned n)
    ensures (result >= n)
{
    return loop_dec(n);
}

// Partial for two reasons: a loop of its own and a partial callee.
verified unsigned both_reasons(unsigned n)
    ensures (result >= n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
    {
        i = i + 1u;
    }
    return calls_partial(i);
}

// Partial: an unsafe block need not return.
verified unsigned through_unsafe(unsigned n)
    ensures (result == n)
{
    unsigned sample = 0u;
    unsafe {
        sample = read_sample();
    }
    return n;
}

// Total: straight-line code needs no measure.
verified unsigned straight(unsigned n)
    expects (n < 100u)
    ensures (result == n + 1u)
{
    return n + 1u;
}

// Total: recursion with a measure every call descends.
verified unsigned count_down(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return count_down(n - 1u);
}

unsigned read_sample() {
    return 7u;
}

int main() {
    const unsigned total = loop_dec(3u) + calls_total(2u) + straight(1u) + count_down(4u);
    const unsigned partial = loop_nodec(0u) + calls_partial(0u) + both_reasons(0u) + through_unsafe(5u);
    return total == 7u && partial == 5u ? 0 : 1;
}
