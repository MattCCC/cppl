// SPEC: TUBOUND-003, CLASS-011
// `room_after` of integration/statement.cpp claiming more than the imported
// contract of `Ledger::free_on_page` gives: a ledger with no lines has 99 lines
// of room, not at most 50.
#include "ledger.hpp"

verified long long room_after(long long total, long long lines)
    expects (total >= -1000000000000ll && total <= 1000000000000ll && lines >= 0ll && lines <= 10000ll)
    ensures (0ll <= result && result <= 50ll)
{
    Ledger ledger{0ll, 0ll};
    ledger.set(total, lines);
    return ledger.free_on_page();
}
