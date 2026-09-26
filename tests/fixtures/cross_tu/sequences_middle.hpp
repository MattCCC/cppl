// A unit between `sequences.cpp` and `sequences_client.cpp`: its contract is
// proven through `three_listed`, whose body used the std::vector model, so its
// own record carries that model on to whoever imports it (SPEC.md TUBOUND-006,
// STDMODEL-018).
#pragma once

#include <cstddef>

verified std::size_t listed_in_the_middle()
    ensures (result == 3ul);
