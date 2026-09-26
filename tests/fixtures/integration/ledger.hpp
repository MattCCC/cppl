// Money, counts and a running ledger, proven in `ledger.cpp` and used by
// `statement.cpp` through the verification interface `ledger.cpp` writes
// (RFC 0017, RFC 0018). The member functions state their contracts here, on
// the class; their out-of-line definitions inherit them (SPEC.md CONTRACT-005).
#pragma once

// One line's amount, in cents.
type Money = long long where (self >= -100000000ll && self <= 100000000ll);

// A statement's total: at most 10000 lines of Money.
type Total = long long where (self >= -1000000000000ll && self <= 1000000000000ll);

// How many lines a ledger has taken.
type Lines = long long where (self >= 0ll && self <= 10000ll);

// Which way an amount moves the balance.
enum class Direction : int { credit = 0, debit = 1 };

struct Ledger {
    Total total;
    Lines lines;

    // A price times a quantity, each read as an `int`: widened before the
    // product, which therefore always fits a `long long`.
    verified long long line_amount(int quantity, int unit_price) const
        ensures (result == static_cast<long long>(quantity) * static_cast<long long>(unit_price));

    // The balance after one more amount, from the refined member's own bound
    // and the refined argument's (SPEC.md REFINE-060, ARITH-006).
    verified long long after(Money amount) const
        ensures (result == total + amount);

    // Writes both members, each owing its refinement.
    verified void set(long long to_total, long long to_lines)
        expects (to_total >= -1000000000000ll && to_total <= 1000000000000ll && to_lines >= 0ll && to_lines <= 10000ll)
        ensures (total == to_total && lines == to_lines);

    // How many more lines fit on the current page. Proven relative to a trusted
    // assumption about the statements this program reads (`ledger.cpp`).
    verified long long free_on_page() const
        ensures (0ll <= result && result <= 99ll);
};

// A magnitude with its direction applied, decided by a case split on the
// direction.
verified long long directed(Direction direction, long long magnitude)
    expects (static_cast<int>(direction) >= 0 && static_cast<int>(direction) <= 1 && magnitude >= 0ll &&
            magnitude <= 999999ll)
    ensures (-999999ll <= result && result <= 999999ll);
