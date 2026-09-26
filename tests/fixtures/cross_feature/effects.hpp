// SPEC: CLASS-011, REFINE-060, TUBOUND-003
// Two contracts proven in `effects.cpp`, alike but for their parameter's
// refinement: `set_five` keeps its `Small&` parameter valid and states 5, and
// `set_fifty` takes a plain `unsigned&` and states 50. A caller in another unit
// that passes a refined place is charged that place's refinement for the value
// either leaves there, from the imported contract alone (TRUST.md TCB-OBJ-009).
// `tests/e2e/cross_feature.sh` uses them through `effects_client.cpp`, and
// `tests/negative/cross_feature.sh` shows the refused twin.
#pragma once

type Small = unsigned where (self < 10u);

verified void set_five(Small& x)
    ensures (x == 5u);

verified void set_fifty(unsigned& x)
    ensures (x == 50u);
