// Reads a statement: lines of "<quantity> <price>" or "<quantity> -<price>",
// prices in cents. This unit sees only `text.hpp` and `ledger.hpp` and the
// verification interfaces `text.cpp` and `ledger.cpp` write, so every call to
// them is proven from the contracts those interfaces record (SPEC.md
// TUBOUND-003) and rests on what their proofs rest on (TUBOUND-006).
//
// Each refused twin in `negative/integration_*.cpp` differs from a function
// here in one thing; `tests/negative/integration_ledger.sh` drives them.
#include "ledger.hpp"
#include "text.hpp"

#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

// SPEC: STDMODEL-012, STDMODEL-013, STDMODEL-016, STDMODEL-020, ARITH-006, ARITH-008, CLASS-011, TUBOUND-003
// The amount of every well-formed line of `text`, summed, with each running
// total appended to `running`. A line whose quantity or price does not parse,
// or whose amount is not `Money`, is skipped. Every position comes from an
// imported contract that keeps it within the span; every amount is widened
// before its product and bounded before it enters a `Money` element; and the
// sum stays within its bound because each element read supplies `Money`'s.
verified long long statement_total(std::span<const char> text, std::vector<long long>& running)
    expects (readable(text) && text.size() <= 10000ul)
    ensures (-1000000000000ll <= result && result <= 1000000000000ll)
{
    const Ledger pricing{0ll, 0ll};
    std::vector<Money> amounts;
    std::size_t at = 0ul;
    while (at < text.size())
        invariant (at <= text.size() && amounts.size() <= at)
        decreases (text.size() - at)
    {
        const std::size_t quantity_from = skip_spaces(text, at);
        const std::size_t quantity_to = digits_end(text, quantity_from);
        const long long quantity = read_number(text, quantity_from, quantity_to);
        std::size_t price_from = skip_spaces(text, quantity_to);
        Direction direction = Direction::credit;
        if (price_from < text.size()) {
            const char sign = text[price_from];
            if (sign == '-') {
                direction = Direction::debit;
                price_from = price_from + 1ul;
            }
        }
        const std::size_t price_to = digits_end(text, price_from);
        const long long magnitude = read_number(text, price_from, price_to);
        const std::size_t end = line_end(text, price_to);
        if (quantity >= 0ll && magnitude >= 0ll) {
            const long long price = directed(direction, magnitude);
            const long long amount = pricing.line_amount(static_cast<int>(quantity), static_cast<int>(price));
            if (amount >= -100000000ll && amount <= 100000000ll) {
                amounts.push_back(amount);
            }
        }
        if (end < text.size()) {
            at = end + 1ul;
        } else {
            at = text.size();
        }
    }
    long long total = 0ll;
    std::size_t line = 0ul;
    while (line < amounts.size())
        invariant (line <= amounts.size() && amounts.size() <= 10000ul &&
                  total >= -100000000ll * static_cast<long long>(line) &&
                  total <= 100000000ll * static_cast<long long>(line))
        decreases (amounts.size() - line)
    {
        const long long amount = amounts[line];
        total = total + amount;
        running.push_back(total);
        ++line;
    }
    return total;
}

// SPEC: CLASS-008, CLASS-011, TUBOUND-006, TRUSTED-002
// A ledger's room on its page, through a member function of another unit
// whose proof rests on a trusted assumption: this claim rests on it too.
verified long long room_after(long long total, long long lines)
    expects (total >= -1000000000000ll && total <= 1000000000000ll && lines >= 0ll && lines <= 10000ll)
    ensures (0ll <= result && result <= 99ll)
{
    Ledger ledger{0ll, 0ll};
    ledger.set(total, lines);
    return ledger.free_on_page();
}

// SPEC: REFINE-060, ARITH-006, CLASS-011
// The balance after one more amount: a refined member plus a refined argument,
// through another unit's member function.
verified long long balance_after(long long total, long long amount)
    expects (total >= -1000000000000ll && total <= 1000000000000ll && amount >= -100000000ll && amount <= 100000000ll)
    ensures (result == total + amount)
{
    Ledger ledger{0ll, 0ll};
    ledger.set(total, 0ll);
    return ledger.after(amount);
}

// SPEC: TUBOUND-006, STDMODEL-014
// How much of a text is read: no more than a setting outside the program
// allows, through another unit's contract whose proof rests on an unsafe block.
verified std::size_t readable_prefix(std::span<const char> text)
    expects (readable(text))
    ensures (result <= text.size())
{
    const std::size_t limit = line_limit();
    if (limit < text.size()) {
        return limit;
    }
    return text.size();
}

// SPEC: STDMODEL-012, STDMODEL-013
// The lines of a string: each character is read under the bound the loop
// keeps, and the count never passes the length.
verified std::size_t count_lines(const std::string& text)
    ensures (result <= text.size())
{
    std::size_t count = 0ul;
    std::size_t i = 0ul;
    while (i < text.size())
        invariant (i <= text.size() && count <= i)
        decreases (text.size() - i)
    {
        const char c = text[i];
        if (c == '\n') {
            ++count;
        }
        ++i;
    }
    return count;
}

int main() {
    const std::string text = "3 125\n2 -250\n10 7\nx 5\n4 99999999\n";
    std::vector<long long> running;
    const std::span<const char> read(text.data(), readable_prefix(text));
    const long long total = statement_total(read, running);
    std::printf("%zu lines, %zu amounts:", count_lines(text), running.size());
    for (const long long value : running) {
        std::printf(" %lld", value);
    }
    std::printf("; total %lld, then %lld, room %lld\n", total, balance_after(total, 55ll),
                room_after(total, static_cast<long long>(running.size())));
    return 0;
}
