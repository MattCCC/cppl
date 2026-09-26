// SPEC: TUBOUND-007
// `counted` in cross_tu/client.cpp, calling count_up instead of count_to.
// library.cpp recorded count_up as partial correctness only, so a function
// asking to terminate cannot rest on it.
#include "library.hpp"

verified unsigned counted(unsigned n)
    ensures (result == n)
    decreases (n)
{
    return count_up(n);
}
