// A unit between two others: it proves doubled using library.cpp's contract of
// clamp4, so its own interface records that it rests on that contract, and a
// unit using doubled must import both interfaces (SPEC.md TUBOUND-006, TUBOUND-009).
#include "middle.hpp"

#include "library.hpp"

unsigned doubled(unsigned y) {
    return clamp4(y) + clamp4(y);
}
