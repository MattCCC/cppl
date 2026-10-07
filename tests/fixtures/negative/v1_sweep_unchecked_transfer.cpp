// SPEC: CLASS-011, TUBOUND-003
// `transfer` of v1_sweep/stock.cpp without its check of the destination's
// room: it pushes onto a shelf that may be full, so the push's precondition is
// not proven, and the claim that the move leaves at most eight orders is false
// of a full destination.
#include "stock.hpp"

verified bool transfer(Shelf& from, Shelf& to)
    ensures (result -> to.size > 0u && to.size <= 8u)
{
    if (from.size == 0u || from.size > 8u) {
        return false;
    }
    const Order moved = from.top();
    from.pop();
    to.push(moved);
    return true;
}
