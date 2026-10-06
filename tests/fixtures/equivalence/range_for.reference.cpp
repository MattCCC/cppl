// Ordinary C++: `range_for.cpp` erased by hand as SPEC.md Annex M says it
// erases. The refinement is the alias of its base type, every contract and loop
// clause is gone, and every range-based for stays as written.
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

using Digit = unsigned;

unsigned sum_vector(const std::vector<unsigned>& v) {
    unsigned total = 0u;
    for (unsigned x : v) {
        total += x;
    }
    return total;
}

unsigned count_spaces(const std::string& text) {
    unsigned spaces = 0u;
    for (char c : text) {
        if (c == ' ') {
            spaces += 1u;
        }
    }
    return spaces;
}

unsigned sum_span(std::span<const unsigned> s) {
    unsigned total = 0u;
    for (unsigned x : s) {
        total += x;
    }
    return total;
}

unsigned sum_arrays() {
    unsigned a[3] = {1u, 2u, 3u};
    std::array<unsigned, 2> b = {4u, 5u};
    unsigned total = 0u;
    for (const unsigned& x : a) {
        total += x;
    }
    for (const auto& y : b) {
        total += y;
    }
    return total;
}

unsigned largest_digit() {
    std::vector<Digit> digits{3u, 9u, 4u};
    unsigned best = 0u;
    for (Digit d : digits) {
        if (d > best) {
            best = d;
        }
    }
    return best;
}

std::size_t reset_digits() {
    std::vector<Digit> digits{3u, 9u};
    for (Digit& d : digits) {
        d = 1u;
    }
    return digits.size();
}

void upcase_a(std::string& text) {
    for (char& c : text) {
        if (c == 'a') {
            c = 'A';
        }
    }
}

unsigned first_over(const std::vector<unsigned>& v, unsigned limit) {
    for (unsigned x : v) {
        if (x <= limit) {
            continue;
        }
        return x;
    }
    return 0u;
}

unsigned sum_to_zero(const std::vector<unsigned>& v) {
    unsigned total = 0u;
    for (unsigned x : v) {
        if (x == 0u) {
            break;
        }
        total += x;
    }
    return total;
}

unsigned pairs(const std::vector<unsigned>& v) {
    unsigned total = 0u;
    for (unsigned x : v) {
        for (unsigned y : v) {
            total += x * y;
        }
    }
    unsigned rounds = 0u;
    while (rounds < 2u) {
        for (unsigned x : v) {
            total += x;
        }
        rounds += 1u;
    }
    return total;
}

unsigned long largest_capped(const unsigned (&values)[3]) {
    unsigned long best = 0ul;
    for (unsigned long value : values) {
        if (value <= 9ul && value > best) {
            best = value;
        }
    }
    return best;
}

int main() {
    const std::vector<unsigned> v{1u, 12u, 0u, 4u};
    const std::vector<unsigned> empty;
    const unsigned values[3] = {4u, 11u, 7u};
    std::string text = "a banana";
    upcase_a(text);
    std::printf("sum_vector(v) == %u, sum_vector(empty) == %u\n", sum_vector(v), sum_vector(empty));
    std::printf("count_spaces(text) == %u\n", count_spaces(text));
    std::printf("sum_span(v) == %u, sum_span(empty) == %u\n", sum_span(v), sum_span(empty));
    std::printf("sum_arrays() == %u\n", sum_arrays());
    std::printf("largest_digit() == %u\n", largest_digit());
    std::printf("reset_digits() == %zu\n", reset_digits());
    std::printf("upcase_a(\"a banana\") == %s\n", text.c_str());
    std::printf("first_over(v, 5) == %u, first_over(v, 20) == %u\n", first_over(v, 5u), first_over(v, 20u));
    std::printf("sum_to_zero(v) == %u\n", sum_to_zero(v));
    std::printf("pairs(v) == %u\n", pairs(v));
    std::printf("largest_capped(values) == %lu\n", largest_capped(values));
    return 0;
}
