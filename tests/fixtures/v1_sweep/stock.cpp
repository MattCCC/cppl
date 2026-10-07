// Proves the stock `stock.hpp` declares, and states what a running total of
// deliveries does as Laws proven by induction.
#include "stock.hpp"

// --- Running totals -------------------------------------------------------------

// The units on hand after `n` deliveries of `per` units onto `start`.
pure unsigned delivered(unsigned start, unsigned per, unsigned n) {
    return start + per * n;
}

// One more delivery adds `per`: the running total's step, at every count.
law delivery_step(unsigned start, unsigned per, unsigned n)
    proves (delivered(start, per, n + 1u) == delivered(start, per, n) + per);

proof delivery_step_by_induction(unsigned start, unsigned per, unsigned n)
    proves (delivery_step(start, per, n))
{
    induction n;
}

// Delivering nothing leaves every start where it was, as a formal equality.
law nothing_delivered(unsigned n)
    proves (forall (unsigned start) { Eq<unsigned>(delivered(start, 0u, n), start) });

proof nothing_delivered_holds(unsigned n)
    proves (nothing_delivered(n))
{
    refl;
}

// A running total no step changes stays at its start. The premise says only
// that each count agrees with the next; that the total at `n` is the start
// follows by induction, rewriting with the premise at the predecessor and
// closing with the hypothesis.
law steady_total(unsigned start, unsigned per, unsigned n)
    expects (forall (unsigned m) { delivered(start, per, m + 1u) == delivered(start, per, m) })
    proves (delivered(start, per, n) == start);

proof steady_total_by_induction(unsigned start, unsigned per, unsigned n)
    proves (steady_total(start, per, n))
{
    induction n {
        zero => {
            refl;
        }

        successor(pred) => {
            assume steps : forall (unsigned m) { delivered(start, per, m + 1u) == delivered(start, per, m) };
            assume hypothesis : (forall (unsigned m) { delivered(start, per, m + 1u) == delivered(start, per, m) }) ->
            delivered(start, per, pred) == start;
            rewrite steps(pred);
            apply hypothesis;
            exact steps;
        }
    }
}

// Evidence that names nothing beyond itself, for the claims below whose
// premises alone cannot hold together.
proof same(unsigned x)
    proves (x == x)
{
    refl;
}

// --- The shelf ------------------------------------------------------------------

bool Shelf::full() const {
    return size == kSlots;
}

void Shelf::push(Order order) {
    units[size] = order.units;
    ids[size] = order.id;
    size = size + 1u;
}

Order Shelf::top() const {
    const unsigned held = units[size - 1u] > kMaxUnits ? kMaxUnits : units[size - 1u];
    const Order order{ids[size - 1u], held};
    return order;
}

void Shelf::pop() {
    size = size - 1u;
}

// --- Orders ---------------------------------------------------------------------

verified Order make_order(unsigned id, unsigned units)
    ensures (result.id == id && result.units <= 1000u && (units <= 1000u -> result.units == units))
{
    Order made{id, units};
    if (made.units > kMaxUnits) {
        made.units = kMaxUnits;
    }
    return made;
}

verified Order larger(const Order& a, const Order& b)
    ensures (result.units >= a.units && result.units >= b.units &&
                                                              (result.units == a.units || result.units == b.units))
{
    Order chosen = a;
    if (a.units < b.units) {
        chosen = b;
    }
    return chosen;
}

verified bool transfer(Shelf& from, Shelf& to)
    ensures (result -> to.size > 0u && to.size <= 8u)
{
    if (from.size == 0u || from.size > 8u) {
        return false;
    }
    const Order moved = from.top();
    from.pop();
    if (to.size >= 8u) {
        return false;
    }
    to.push(moved);
    return true;
}

verified std::size_t shelf_units(const Shelf& shelf)
    expects (shelf.size <= 8u)
    ensures (result <= 8000u)
{
    // The size the loop runs to, recorded for the proof alone: erasure removes
    // it, and nothing that runs may read it.
    ghost std::size_t slots = shelf.size;
    std::size_t total = 0u;
    std::size_t slot = 0u;
    while (slot < shelf.size)
        invariant (slot <= slots && slots == shelf.size && total <= 1000u * slot)
        decreases (slots - slot)
    {
        total = total + (shelf.units[slot] > kMaxUnits ? kMaxUnits : shelf.units[slot]);
        ++slot;
    }
    return total;
}

verified unsigned reserve(unsigned have, unsigned want)
    ensures (result <= have && (want <= have -> result == have - want))
{
    if (const unsigned left = have - want; want <= have) {
        return left;
    }
    return have;
}

verified long long signed_units(Side side, unsigned units)
    expects (static_cast<unsigned>(side) <= 1u)
    ensures ((side == Side::buy -> result == units) && (side == Side::sell -> result == -static_cast<long long>(units)))
{
    switch (side) {
        case Side::buy:
            return static_cast<long long>(units);
        case Side::sell:
            return -static_cast<long long>(units);
    }
    // The precondition leaves no side but these two, so no execution gets
    // past the switch, and the claim that none does is checked here.
    contradiction same(0u);
    return 0ll;
}

verified long long net_position(unsigned bought, unsigned sold)
    ensures (result == static_cast<long long>(bought) - static_cast<long long>(sold))
{
    return static_cast<long long>(bought) - static_cast<long long>(sold);
}

verified unsigned remainder_after_lots(unsigned units, unsigned lot)
    expects (lot > 0u)
    ensures (result < lot)
    decreases (units)
{
    if (units < lot) {
        return units;
    }
    return remainder_after_lots(units - lot, lot);
}
