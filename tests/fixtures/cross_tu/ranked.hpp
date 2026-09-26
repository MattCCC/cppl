// The public contracts of ranked.cpp, for the cross-unit totality and
// definition cases of negative/cross_tu.sh (SPEC.md TUBOUND-004).
#pragma once

pure unsigned base() {
    return 1u;
}

// Reaches `base`: a contract mentioning it states `base` too, by content.
pure unsigned limit() {
    return base() + 3u;
}

// Total: it asks to terminate, and ranked.cpp proves it with this measure.
verified unsigned count_down(unsigned n)
    ensures (result == 0u)
    decreases (n);

verified unsigned bounded(unsigned x)
    expects (x < limit())
    ensures (result < limit());
