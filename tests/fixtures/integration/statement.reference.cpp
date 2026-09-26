// integration/statement.cpp with every C++L construct erased by hand,
// declaring what text.hpp and ledger.hpp declare as ordinary C++ does. Using
// another unit's contracts changes nothing that runs (SPEC.md TUBOUND-003,
// ERASE-002).
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

std::size_t skip_spaces(std::span<const char> in, std::size_t at);
std::size_t digits_end(std::span<const char> in, std::size_t at);
std::size_t line_end(std::span<const char> in, std::size_t at);
long long read_number(std::span<const char> in, std::size_t from, std::size_t to);
std::size_t line_limit();

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

long long statement_total(std::span<const char> text, std::vector<long long>& running) {
    const Ledger pricing{0ll, 0ll};
    std::vector<Money> amounts;
    std::size_t at = 0ul;
    while (at < text.size()) {
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
    while (line < amounts.size()) {
        const long long amount = amounts[line];
        total = total + amount;
        running.push_back(total);
        ++line;
    }
    return total;
}

long long room_after(long long total, long long lines) {
    Ledger ledger{0ll, 0ll};
    ledger.set(total, lines);
    return ledger.free_on_page();
}

long long balance_after(long long total, long long amount) {
    Ledger ledger{0ll, 0ll};
    ledger.set(total, 0ll);
    return ledger.after(amount);
}

std::size_t readable_prefix(std::span<const char> text) {
    const std::size_t limit = line_limit();
    if (limit < text.size()) {
        return limit;
    }
    return text.size();
}

std::size_t count_lines(const std::string& text) {
    std::size_t count = 0ul;
    std::size_t i = 0ul;
    while (i < text.size()) {
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
