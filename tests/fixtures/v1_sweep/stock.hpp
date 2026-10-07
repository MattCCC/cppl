// The stock a small trading desk keeps: orders of a bounded quantity on a
// shelf of eight slots, proven in `stock.cpp` and used by `intake.cpp` and
// `desk.cpp` through the verification interface `stock.cpp` writes (SPEC.md
// TUBOUND-002, TUBOUND-003). The member functions state their contracts here,
// on the class; their out-of-line definitions inherit them (SPEC.md
// CONTRACT-005).
#pragma once

#include <array>
#include <cstddef>

// The most units one order may carry, and how many orders a shelf holds.
constexpr unsigned kMaxUnits = 1000u;
constexpr std::size_t kSlots = 8u;

// The price levels the desk quotes.
enum Levels : unsigned { kLevels = 4u };

// What one order may carry.
type Quantity = unsigned where (self <= 1000u);

// Which side of the book an order stands on.
enum class Side : unsigned { buy, sell };

struct Order {
    unsigned id;
    unsigned units;
};

// A stack of orders: the units of each in a std::array, their ids in a
// built-in array, both members of the object each member function runs on.
// Without `old(...)` a postcondition states the post-state only, so a caller
// that pops twice checks the size again in between.
struct Shelf {
    std::array<unsigned, 8> units;
    unsigned ids[8];
    std::size_t size;

    verified bool full() const
        ensures (result == (size == 8u));

    verified void push(Order order)
        expects (size < 8u)
        ensures (size > 0u && size <= 8u);

    // The order on top, its units clamped to what one order may carry: nothing
    // states what every slot holds, so the bound is the clamp's.
    verified Order top() const
        expects (size > 0u && size <= 8u)
        ensures (result.units <= 1000u);

    verified void pop()
        expects (size > 0u && size <= 8u)
        ensures (size < 8u);
};

// An order of `units`, clamped to what one order may carry.
verified Order make_order(unsigned id, unsigned units)
    ensures (result.id == id && result.units <= 1000u && (units <= 1000u -> result.units == units));

// The order of two with more units, the first where they tie.
verified Order larger(const Order& a, const Order& b)
    ensures (result.units >= a.units && result.units >= b.units && (result.units == a.units || result.units == b.units));

// Moves the order on top of `from` onto `to` where both allow it, and says
// whether it did. The two may be one shelf, so the room on `to` is read after
// the pop, which may have changed it.
verified bool transfer(Shelf& from, Shelf& to)
    ensures (result -> to.size > 0u && to.size <= 8u);

// The units on a shelf, each slot counted at most `kMaxUnits`.
verified std::size_t shelf_units(const Shelf& shelf)
    expects (shelf.size <= 8u)
    ensures (result <= 8000u);

// What is left of `have` once `want` is reserved, by default one unit.
verified unsigned reserve(unsigned have, unsigned want = 1u)
    ensures (result <= have && (want <= have -> result == have - want));

// Which way an order on `side` moves the desk's position, in units.
verified long long signed_units(Side side, unsigned units)
    expects (static_cast<unsigned>(side) <= 1u)
    ensures ((side == Side::buy -> result == units) && (side == Side::sell -> result == -static_cast<long long>(units)));

// The position after buying `bought` and selling `sold`.
verified long long net_position(unsigned bought, unsigned sold)
    ensures (result == static_cast<long long>(bought) - static_cast<long long>(sold));

// What is left after taking whole lots of `lot` units from `units`, one lot
// at a time.
verified unsigned remainder_after_lots(unsigned units, unsigned lot)
    expects (lot > 0u)
    ensures (result < lot)
    decreases (units);
