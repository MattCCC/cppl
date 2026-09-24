// SPEC: GHOST-001
// A verified function's call is modeled by its contract as something that runs.
// In a ghost initializer it would not run, so it is refused like any call to a
// function the formal core does not define.
verified unsigned next(unsigned x)
    expects (x < 10u)
    ensures (result == x + 1u)
{
    return x + 1u;
}

verified unsigned uses(unsigned x)
    expects (x < 5u)
    ensures (result == x)
{
    ghost unsigned g = next(x);
    return x;
}

int main() {
    return static_cast<int>(uses(2u));
}
