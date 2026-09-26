// SPEC: TUBOUND-004
// `stepped` in cross_tu/client.cpp, expecting the `unsigned` overload's
// contract from a call Clang resolves to the `unsigned long` one. Each overload
// carries its own recorded contract, and this one adds one.
#include "library.hpp"

verified unsigned long stepped(unsigned long wide)
    ensures (result == wide)
{
    return step(wide);
}
