// Contracts another unit compares by meaning (SPEC.md TUBOUND-004), proven in
// `identity.cpp`. `stepped` states its postcondition through a pure function
// that calls another, so its identity must include both; `counted_down` asks to
// terminate, and proves it under a lexicographic measure a consumer need not
// repeat. `tests/negative/cross_tu.sh` pairs each with a consumer that states
// the same contract and one that states another.
#pragma once

inline pure unsigned inner_step(unsigned x) {
    return x + 1u;
}

inline pure unsigned outer_step(unsigned x) {
    return inner_step(x) + 1u;
}

verified unsigned stepped(unsigned x)
    expects (x < 100u)
    ensures (result == outer_step(x));

verified unsigned counted_down(unsigned n)
    ensures (result == n)
    decreases (n, n);
