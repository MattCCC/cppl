// SPEC: STDMODEL-012, STDMODEL-016, TUBOUND-003
// `statement_total` of integration/statement.cpp with its sign check one past
// the end: where `price_from` is the span's size, `text[price_from]` reads past
// the last character. The imported contracts bound `price_from` by the size and
// no further, so the subscript's bound is not proven.
#include "ledger.hpp"
#include "text.hpp"

#include <cstddef>
#include <span>
#include <vector>

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
        if (price_from <= text.size()) {
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
