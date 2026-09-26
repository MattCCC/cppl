// SPEC: TUBOUND-005
// cross_tu/library.cpp after an edit that breaks clamp4: it now returns its
// argument unchanged. An interface produced from the file before this edit is
// stale, and a unit importing it must not keep using the contract it records.
// The unit itself no longer verifies, so no current interface can exist.
#include "library.hpp"

unsigned clamp4(unsigned x) {
    return x;
}
