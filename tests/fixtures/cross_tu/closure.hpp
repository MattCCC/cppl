// Four contracts proven in `closure.cpp`, alike but for what their proofs rest
// on: nothing, a trusted law, the std::vector model, and an unsafe block. A
// claim of another unit proven through one is free of trusted assumptions only
// where the record's closure is, and lists the record as an external verified
// dependency either way (SPEC.md TUBOUND-006). `tests/e2e/cross_tu.sh` drives
// them through `closure_client.cpp`, directly and through `closure_middle.cpp`.
#pragma once

verified unsigned plain(unsigned x)
    expects (x < 100u)
    ensures (result == x + 1u);

verified unsigned trusting(unsigned x)
    expects (x < 10u)
    ensures (result != 7u);

verified unsigned modeled()
    ensures (result == 2u);

verified unsigned unsafe_read()
    ensures (result <= 100u);
