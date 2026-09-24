// SPEC: TERMINATION-007
// A cycle through two functions must descend on every edge. `ping` calls
// `pong` at the measure it was entered with, so that edge does not descend,
// even though the round trip does.
unsigned pong(unsigned n);

verified unsigned ping(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return pong(n);
}

verified unsigned pong(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return ping(n - 1u);
}

int main() {
    return static_cast<int>(ping(3u));
}
