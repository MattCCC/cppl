// Proves the contracts ranked.hpp declares, and records them.
#include "ranked.hpp"

unsigned count_down(unsigned n) {
    if (n == 0u) {
        return 0u;
    }
    return count_down(n - 1u);
}

unsigned bounded(unsigned x) {
    return x;
}
