// A unit between `closure.cpp` and `closure_client.cpp`: each contract is
// proven through one of `closure.cpp`, and its record carries what that one
// rests on to whoever imports it (SPEC.md TUBOUND-006).
#pragma once

verified unsigned relayed_plain(unsigned x)
    expects (x < 100u)
    ensures (result == x + 1u);

verified unsigned relayed_trusting(unsigned x)
    expects (x < 10u)
    ensures (result != 7u);

verified unsigned relayed_unsafe()
    ensures (result <= 100u);
