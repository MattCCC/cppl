// Proves the ledger `ledger.hpp` declares.
#include "ledger.hpp"

// The statements this program reads put at most 99 lines on a page. Nothing
// here checks the source that produced them, and as a statement about every
// count it is false, so it is stated as trusted: every claim that rests on it,
// in this unit and in every unit that uses a contract proven through it, is
// proven only relative to it and says so (SPEC.md TRUSTED-002, TUBOUND-006).
trusted law page_holds(long long lines_on_page)
    proves (lines_on_page <= 99ll);

// The assumption applied to one count, as the evidence of a claim.
proof page_bound(long long used)
    proves (used <= 99ll)
{
    exact page_holds(used);
}

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

long long Ledger::line_amount(int quantity, int unit_price) const {
    return static_cast<long long>(quantity) * static_cast<long long>(unit_price);
}

long long Ledger::after(Money amount) const {
    return total + amount;
}

void Ledger::set(long long to_total, long long to_lines) {
    total = to_total;
    lines = to_lines;
}

// The room left on a page holding `used` lines. More than 99 is allowed by the
// type, so without the trusted assumption this contract is not proven
// (`negative/integration_untrusted_page.cpp`). A claim that a path cannot
// occur is written in a function marked `verified`, which an out-of-line
// member definition cannot be (`negative/integration_out_of_line_claim.cpp`),
// so the member function below calls this one.
verified long long page_room(long long used)
    expects (used >= 0ll)
    ensures (0ll <= result && result <= 99ll)
{
    if (used > 99ll) {
        contradiction page_bound(used);
    }
    return 99ll - used;
}

long long Ledger::free_on_page() const {
    return page_room(lines);
}

verified long long directed(Direction direction, long long magnitude)
    expects (static_cast<int>(direction) >= 0 && static_cast<int>(direction) <= 1 && magnitude >= 0ll &&
            magnitude <= 999999ll)
    ensures (-999999ll <= result && result <= 999999ll)
{
    cases direction {
        Direction::credit => {
        }

        Direction::debit => {
        }

        omit unnamed by contradiction same(0u);
    }
    if (direction == Direction::debit) {
        return -magnitude;
    }
    return magnitude;
}
