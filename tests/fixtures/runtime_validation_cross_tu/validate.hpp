// The public contract of validate.cpp (SPEC.md TU-001, TUBOUND-002), whose
// proof rests on a runtime validation site (SPEC.md RUNTIMECHECK-015).
#pragma once

type Positive = int where (self > 0);

// Proven because the value entered Positive only where a runtime check held.
verified int positive_or_one(int raw)
    ensures (result > 0);

// Proven outright: no check establishes what it returns.
verified int always_two(int raw)
    ensures (result == 2);
