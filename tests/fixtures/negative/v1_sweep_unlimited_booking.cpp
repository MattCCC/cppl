// SPEC: TUBOUND-003, TUBOUND-006, CLASS-011
// `book` of v1_sweep/desk.cpp without its check of the shelf's room: an
// acceptable request is pushed onto a shelf that may already hold eight
// orders, so the precondition another unit's contract of `Shelf::push` states
// is not proven where the call is made.
#include "intake.hpp"
#include "stock.hpp"

#include <cstddef>
#include <vector>

verified std::size_t book(Shelf& shelf, const std::vector<unsigned>& requested, unsigned first_id)
    expects (shelf.size <= 8u)
    ensures (shelf.size <= 8u && result <= requested.size() && (result > 0u -> shelf.size > 0u))
{
    std::size_t booked = 0u;
    for (std::size_t i = 0u; i < requested.size(); ++i)
        invariant (i <= requested.size() && booked <= i && shelf.size <= 8u && (booked > 0u -> shelf.size > 0u))
        decreases (requested.size() - i)
    {
        const unsigned units = accepted_units(requested[i]);
        if (units > 0u) {
            const Order order = make_order(first_id + static_cast<unsigned>(booked), units);
            shelf.push(order);
            ++booked;
        }
    }
    return booked;
}

int main() {
    return 0;
}
