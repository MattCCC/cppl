// A tokenizer over a line of text, proven in `text.cpp` and used by
// `statement.cpp` through the verification interface `text.cpp` writes
// (RFC 0017). Every position a contract returns stays within the span it was
// given, so a caller's subscripts are bounded by these contracts alone.
//
// Part of the integration fixture set `tests/e2e/integration_ledger.sh` drives:
// several translation units, non-virtual methods, refined money, count and
// index values, signed arithmetic, spans, vectors and strings, loops with
// invariants and measures, a case split, and a trusted and an unsafe boundary
// the trust report names across units.
#pragma once

#include <cstddef>
#include <span>

// The first position at or after `at` that is not a space.
verified std::size_t skip_spaces(std::span<const char> in, std::size_t at)
    expects (readable(in) && at <= in.size())
    ensures (at <= result && result <= in.size());

// The first position at or after `at` that is not a decimal digit.
verified std::size_t digits_end(std::span<const char> in, std::size_t at)
    expects (readable(in) && at <= in.size())
    ensures (at <= result && result <= in.size());

// The first position at or after `at` that holds a newline, or the end.
verified std::size_t line_end(std::span<const char> in, std::size_t at)
    expects (readable(in) && at <= in.size())
    ensures (at <= result && result <= in.size());

// The value of the decimal digits in [from, to), or -1 when there are none,
// one of them is not a digit, or there are more than six. Signed arithmetic:
// every step owes that it does not overflow.
verified long long read_number(std::span<const char> in, std::size_t from, std::size_t to)
    expects (readable(in) && from <= to && to <= in.size())
    ensures (-1ll <= result && result <= 999999ll);

// How much of a text this program reads, from a setting outside it. The
// setting is read in an unsafe block and clamped, so the contract holds
// whatever it is; the claim still rests on that block, and says so.
verified std::size_t line_limit()
    ensures (1ul <= result && result <= 4096ul);
