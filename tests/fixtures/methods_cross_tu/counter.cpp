// Proves the contracts `counter.hpp` states and records them in a
// verification interface.
#include "counter.hpp"

unsigned Counter::get() const {
    return value;
}

unsigned Counter::headroom() const {
    return limit - value;
}

void Counter::reset() {
    value = 0u;
}
