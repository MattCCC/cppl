// Proves the tokenizer `text.hpp` declares. A definition with loop clauses
// restates its declaration's contract, which must mean the same (SPEC.md
// TU-003).
#include "text.hpp"

verified std::size_t skip_spaces(std::span<const char> in, std::size_t at)
    expects (readable(in) && at <= in.size())
    ensures (at <= result && result <= in.size())
{
    std::size_t i = at;
    while (i < in.size())
        invariant (at <= i && i <= in.size())
        decreases (in.size() - i)
    {
        const char c = in[i];
        if (c != ' ') {
            return i;
        }
        ++i;
    }
    return i;
}

verified std::size_t digits_end(std::span<const char> in, std::size_t at)
    expects (readable(in) && at <= in.size())
    ensures (at <= result && result <= in.size())
{
    std::size_t i = at;
    while (i < in.size())
        invariant (at <= i && i <= in.size())
        decreases (in.size() - i)
    {
        const char c = in[i];
        if (c < '0' || c > '9') {
            return i;
        }
        ++i;
    }
    return i;
}

verified std::size_t line_end(std::span<const char> in, std::size_t at)
    expects (readable(in) && at <= in.size())
    ensures (at <= result && result <= in.size())
{
    std::size_t i = at;
    while (i < in.size())
        invariant (at <= i && i <= in.size())
        decreases (in.size() - i)
    {
        const char c = in[i];
        if (c == '\n') {
            return i;
        }
        ++i;
    }
    return i;
}

verified long long read_number(std::span<const char> in, std::size_t from, std::size_t to)
    expects (readable(in) && from <= to && to <= in.size())
    ensures (-1ll <= result && result <= 999999ll)
{
    if (from == to) {
        return -1ll;
    }
    long long value = 0ll;
    std::size_t i = from;
    while (i < to)
        invariant (from <= i && i <= to && 0ll <= value && value <= 999999ll)
        decreases (to - i)
    {
        const char c = in[i];
        if (c < '0' || c > '9') {
            return -1ll;
        }
        if (value > 99999ll) {
            return -1ll;
        }
        value = value * 10ll + (c - '0');
        ++i;
    }
    return value;
}

unsafe std::size_t configured_line_limit();

verified std::size_t line_limit()
    ensures (1ul <= result && result <= 4096ul)
{
    std::size_t limit = 0ul;
    unsafe {
        limit = configured_line_limit();
    }
    if (limit < 1ul || limit > 4096ul) {
        return 4096ul;
    }
    return limit;
}

unsafe std::size_t configured_line_limit() {
    return 256ul;
}
