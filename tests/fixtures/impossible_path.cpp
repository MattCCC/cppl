// Runtime paths claimed not to occur (SPEC.md VERIFIED-023, GRAMMAR.md 5.6).
//
// `contradiction evidence;` written in a verified body claims that no execution
// reaches it: the facts established on the way there - preconditions, branch
// conditions, loop invariants, callee postconditions - cannot all hold together
// with what the named evidence establishes. The claim is an obligation of its
// own, discharged by the same checked contradiction a proof uses, and the path
// ends there, so nothing after it on that path owes anything. At runtime the
// statement is an empty statement.
#include <cstdio>

pure unsigned zero() {
    return 0u;
}

// Evidence that contributes nothing of its own. Every claim below is carried by
// the facts of its path, which is the common case: the named evidence has to be
// checked, but it need not be what contradicts them.
proof nothing()
    proves (zero() == 0u)
{
    refl;
}

// Evidence instantiated at a value, to pin that its arguments are read where
// the claim stands.
proof pinned(unsigned v)
    proves (v == v)
{
    refl;
}

// The precondition and the branch condition contradict each other.
verified void check(int x)
    expects (x >= 0)
{
    if (x < 0) {
        contradiction nothing;
    }
}

// The claim as the whole body of an `if`, unbraced. Its `;` stays in the
// program, so the `return` below stays outside the `if`.
verified unsigned unbraced(unsigned x)
    expects (x < 5u)
    ensures (result == x)
{
    if (x >= 5u)
        contradiction nothing;
    return x;
}

// What follows the claim on its path owes nothing: the return below would break
// the postcondition, and no execution reaches it. The refused twin, whose
// condition some `x` satisfies, is
// `fixtures/negative/impossible_path_reachable.cpp`.
verified unsigned after(unsigned x)
    expects (x < 5u)
    ensures (result < 5u)
{
    if (x >= 5u) {
        contradiction nothing;
        return 7u;
    }
    return x;
}

// The evidence's argument is the local's current version, `x + 1`.
verified unsigned versions(unsigned x)
    expects (x == 3u)
    ensures (result == 3u)
{
    unsigned y = x;
    y = y + 1u;
    if (y != 4u) {
        contradiction pinned(y);
    }
    return y - 1u;
}

// Inside a loop, the invariant and the loop condition are facts of the path.
verified unsigned count(unsigned n)
    expects (n < 100u)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
    {
        if (i > n) {
            contradiction nothing;
        }
        i = i + 1u;
    }
    return i;
}

// A verified callee's postcondition is a fact of the path once its call is
// made: `y == x + 1` with `x < 5` rules out `y == 0`.
verified unsigned successor(unsigned x)
    expects (x < 10u)
    ensures (result == x + 1u)
{
    return x + 1u;
}

verified unsigned uses_call(unsigned x)
    expects (x < 5u)
    ensures (result == x + 1u)
{
    unsigned y = successor(x);
    if (y == 0u) {
        contradiction nothing;
    }
    return y;
}

int main() {
    check(1);
    std::printf("%u %u %u %u %u\n", unbraced(3u), after(2u), versions(3u), count(7u), uses_call(3u));
}
