// SPEC: TUBOUND-004, TEMPLATE-003
// `bounded` in cross_tu/client.cpp, through bound<5u> rather than bound<4u>.
// bound<5u> is declared with exactly the contract library.cpp proved for
// bound<4u>, and is still another function: a proof of one specialization is
// never a proof of another.
#include "library.hpp"

template <>
verified unsigned bound<5u>(unsigned x)
    ensures (result < 4u);

verified unsigned bounded(unsigned x)
    ensures (result < 4u)
{
    return bound<5u>(x);
}
