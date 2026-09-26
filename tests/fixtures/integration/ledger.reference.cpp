// integration/ledger.hpp and ledger.cpp with every C++L construct erased by
// hand. The refinements are their base types, the trusted law and the proof
// leave nothing, a claim that a path cannot occur leaves its `;`, and the case
// split leaves an empty statement (SPEC.md ERASE-002, ERASE-010, ERASE-016).
using Money = long long;
using Total = long long;
using Lines = long long;

enum class Direction : int { credit = 0, debit = 1 };

struct Ledger {
    Total total;
    Lines lines;

    long long line_amount(int quantity, int unit_price) const;
    long long after(Money amount) const;
    void set(long long to_total, long long to_lines);
    long long free_on_page() const;
};

long long directed(Direction direction, long long magnitude);

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

long long page_room(long long used) {
    if (used > 99ll) {
        ;
    }
    return 99ll - used;
}

long long Ledger::free_on_page() const {
    return page_room(lines);
}

long long directed(Direction direction, long long magnitude) {
    ;
    if (direction == Direction::debit) {
        return -magnitude;
    }
    return magnitude;
}
