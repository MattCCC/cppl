// SPEC: CLASS-011, REFINE-060, TUBOUND-003
// `Gauge::assign_five` of fixtures/cross_feature/effects_client.cpp, calling
// the other unit's `set_fifty` instead. Its parameter is a plain `unsigned&`
// and its contract states 50, so the value it leaves in `level` enters `Small`
// unshown, and the caller is charged that at the call: an imported contract
// establishes no refinement it does not state (TRUST.md TCB-OBJ-009).
#include "effects.hpp"

struct Gauge {
    Small level;

    verified void assign_fifty()
        ensures (level == 50u)
    {
        set_fifty(level);
    }
};
