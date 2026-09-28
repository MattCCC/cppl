// Refused twin of fixtures/runtime_validation_cross_tu/relay.cpp (tests/negative/refused_twins.sh): the same
// program, except that its first function returns one less than through_interface.
// Uses consume.cpp's contract, which was proven through validate.cpp's, whose
// proof rests on a runtime check. The site is carried through both units, and
// a record of validate.cpp from which it was edited away is not the record
// consume.cpp was proven through (SPEC.md RUNTIMECHECK-015, TUBOUND-009).
#include "consume.hpp"

verified int relayed(int raw)
    ensures (result > 0)
{
    return through_interface(raw) - 1;
}
