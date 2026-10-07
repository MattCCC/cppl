// The desk: it reads its clients' requests, books the acceptable ones onto a
// shelf, prices them and moves them on. This unit sees only `stock.hpp` and
// `intake.hpp` and the verification interfaces `stock.cpp` and `intake.cpp`
// write, so every call to them is proven from the contracts those interfaces
// record (SPEC.md TUBOUND-003) and rests on what their proofs rest on
// (TUBOUND-006): the std::array model the shelf uses, the validation of a
// request's units, and the unsafe block that reads the desk's configuration.
//
// Each refused twin in `tests/negative/v1_sweep.sh` changes one thing here or
// in the units this one uses.
#include "intake.hpp"
#include "stock.hpp"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

// The price of a level of the ladder. A signed level names an element only
// where it is neither negative nor past the end (SPEC.md STORAGE-005), and
// the claim is that every level the precondition admits does.
verified unsigned quote(int level)
    expects (0 <= level && level < 4)
    ensures (true)
{
    const unsigned ladder[kLevels] = {100u, 101u, 103u, 107u};
    return ladder[level];
}

// The bands an order's fee is set by, one per 250 units.
enum Band : unsigned { kSmall = 0u, kMedium = 1u, kLarge = 2u, kBulk = 3u };

// The fee per unit for an order's size: a switch on a condition variable,
// labelled by an enumeration and falling through from one band to the next.
// Only an order of exactly 1000 units reaches the default.
verified unsigned fee_per_unit(unsigned units)
    expects (units <= 1000u)
    ensures (result >= 1u && result <= 4u)
{
    switch (const unsigned band = units / 250u) {
        case kSmall:
            return 4u;
        case kMedium:
            return 3u;
        case kLarge:
            [[fallthrough]];
        case kBulk:
            return 2u;
        default:
            return band - 3u;
    }
}

// Where on a shelf an order is, or the shelf's size where it is not: the loop
// stops when it finds the order, and its invariant says that it only stops
// before the end when it has.
verified std::size_t slot_of(const Shelf& shelf, unsigned id)
    expects (shelf.size <= 8u)
    ensures (result <= shelf.size)
{
    std::size_t slot = 0u;
    bool found = false;
    while (slot < shelf.size && !found)
        invariant (slot <= shelf.size && (found -> slot < shelf.size))
        decreases (shelf.size - slot, found ? 0u : 1u)
    {
        if (shelf.ids[slot] == id) {
            found = true;
        } else {
            ++slot;
        }
    }
    return found ? slot : shelf.size;
}

// The units in a slot, or none past the shelf's top: the element is read only
// on the arm that selects it.
verified unsigned units_in(const Shelf& shelf, std::size_t slot)
    expects (shelf.size <= 8u)
    ensures (slot >= shelf.size -> result == 0u)
{
    return slot < shelf.size ? shelf.units[slot] : 0u;
}

// Whether a slot holds at least `wanted` units: the element is read only where
// the slot is on the shelf.
verified bool covers(const Shelf& shelf, std::size_t slot, unsigned wanted)
    expects (shelf.size <= 8u)
    ensures (result -> slot < shelf.size)
{
    return slot < shelf.size && shelf.units[slot] >= wanted;
}

// Whether a shelf can neither be popped nor pushed onto, as a value.
verified bool idle_or_full(const Shelf& shelf)
    ensures (result <-> (shelf.size == 0u || shelf.size == 8u))
{
    const bool idle = shelf.size == 0u || shelf.size == kSlots;
    return idle;
}

// The room a shelf has left, from an init-statement.
verified std::size_t room_left(const Shelf& shelf)
    expects (shelf.size <= 8u)
    ensures (result == 8u - shelf.size)
{
    if (const std::size_t used = shelf.size; used < kSlots) {
        return kSlots - used;
    }
    return 0u;
}

// Books every request whose units one order may carry, while the shelf has
// room and the desk's configuration allows, numbering the orders from
// `first_id`. The configuration is read in another unit's unsafe block, so
// this claim rests on it.
verified std::size_t book(Shelf& shelf, const std::vector<unsigned>& requested, unsigned first_id)
    expects (shelf.size <= 8u)
    ensures (shelf.size <= 8u && result <= requested.size() && (result > 0u -> shelf.size > 0u))
{
    const std::size_t limit = configured_slots();
    std::size_t booked = 0u;
    for (std::size_t i = 0u; i < requested.size(); ++i)
        invariant (i <= requested.size() && booked <= i && shelf.size <= 8u && (booked > 0u -> shelf.size > 0u))
        decreases (requested.size() - i)
    {
        const unsigned units = accepted_units(requested[i]);
        if (units > 0u && shelf.size < limit) {
            const Order order = make_order(first_id + static_cast<unsigned>(booked), units);
            shelf.push(order);
            ++booked;
        }
    }
    return booked;
}

// The units a list of requests asks for, each counted only where one order
// may carry it, and the total kept within its bound.
verified unsigned requested_total(const std::vector<unsigned>& requested)
    ensures (result <= 1000000u)
{
    unsigned total = 0u;
    for (const unsigned units : requested)
        invariant (total <= 1000000u)
    {
        const unsigned accepted = accepted_units(units);
        if (total <= 999000u) {
            total = total + accepted;
        }
    }
    return total;
}

// A fresh shelf, value-initialized, takes the order on top of `from`, and
// holds what the move left on it.
verified std::size_t restock_from(Shelf& from)
    ensures (result <= 8u)
{
    Shelf fresh{};
    const bool moved = transfer(from, fresh);
    return moved ? fresh.size : 0u;
}

// The larger of the two orders on top of a shelf, popped in turn. The size is
// read again after the first pop, which a postcondition without `old(...)`
// states only as below the shelf's size.
verified unsigned larger_of_top_two(Shelf& shelf)
    expects (shelf.size <= 8u)
    ensures (result <= 1000u)
{
    if (shelf.size < 2u) {
        return 0u;
    }
    const Order first = shelf.top();
    shelf.pop();
    if (shelf.size == 0u) {
        return first.units;
    }
    const Order second = shelf.top();
    Order best = larger(first, second);
    shelf.pop();
    return best.units;
}

// The units left of a stock once one is sold, through the default argument
// another unit's declaration gives.
verified unsigned after_one_sale(unsigned on_hand)
    ensures (result <= on_hand && (on_hand > 0u -> result == on_hand - 1u))
{
    return reserve(on_hand);
}

// What an order leaves once whole lots of 250 units are taken from it, by a
// recursion another unit proves terminates.
verified unsigned odd_lot(unsigned units)
    ensures (result < 250u)
{
    return remainder_after_lots(units, 250u);
}

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

// The desk's position after one more order: signed arithmetic over another
// unit's signed result, kept within `long long` by the bounds stated. That
// result is stated side by side, so the path is split by side first, and
// each arm supposes the premise of one implication; the precondition leaves
// no unnamed side.
verified long long position_after(long long position, Side side, unsigned units)
    expects (position >= -1000000ll && position <= 1000000ll && units <= 1000u && static_cast<unsigned>(side) <= 1u)
    ensures (-1001000ll <= result && result <= 1001000ll)
{
    cases side {
        Side::buy => {
        }

        Side::sell => {
        }

        omit unnamed by contradiction same(0u);
    }
    return position + signed_units(side, units);
}

// The requests of one session, each a side and the units it asks for: an
// order past what one may carry, a line with no units, and a sale of none
// among them.
int main() {
    const std::vector<std::string> lines{"b120", "s5000", "b7", "x", "s0", "b250", "s999"};
    std::vector<unsigned> requested;
    long long position = 0ll;
    for (const std::string& line : lines) {
        const std::string digits = line.size() > 1u ? line.substr(1) : std::string();
        const unsigned units = parse_units(digits);
        requested.push_back(units);
        position = position_after(position, side_of(line), accepted_units(units));
    }
    Shelf shelf{};
    const std::size_t booked = book(shelf, requested, 100u);
    std::printf("booked %zu of %zu, %u units accepted, position %lld\n", booked, requested.size(),
                requested_total(requested), position);
    std::printf("slot of 101: %zu, units %u, covers %d, room %zu, idle or full %d\n", slot_of(shelf, 101u),
                units_in(shelf, 1u), covers(shelf, 0u, 100u) ? 1 : 0, room_left(shelf), idle_or_full(shelf) ? 1 : 0);
    // Each of these changes the shelf, so each is a statement of its own.
    const std::size_t held = shelf_units(shelf);
    const unsigned larger = larger_of_top_two(shelf);
    const std::size_t restocked = restock_from(shelf);
    std::printf("shelf holds %zu units; larger of top two %u; restocked %zu, %zu left\n", held, larger, restocked,
                shelf.size);
    std::printf("fees %u %u %u, quote %u, after a sale %u, odd lot %u, net %lld\n", fee_per_unit(120u),
                fee_per_unit(600u), fee_per_unit(1000u), quote(2), after_one_sale(7u), odd_lot(999u),
                net_position(3u, 5u));
    return 0;
}
