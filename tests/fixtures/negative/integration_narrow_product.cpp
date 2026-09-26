// SPEC: ARITH-006, DEFINEDBEHAVIOR-001, CLASS-008
// `Ledger::line_amount` of integration/ledger.hpp with the product taken in
// `int` rather than widened first: 999999 * 999999 does not fit an `int`, so
// the multiplication owes a no-overflow obligation nothing proves.
using Total = long long;

struct Ledger {
    Total total;

    verified long long line_amount(int quantity, int unit_price) const
        ensures (result == quantity * unit_price)
    {
        return quantity * unit_price;
    }
};

int main() {
    const Ledger ledger{0ll};
    return static_cast<int>(ledger.line_amount(2, 3)) - 6;
}
