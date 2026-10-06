// Refused twin of fixtures/equivalence/range_for.cpp (tests/negative/refused_twins.sh): the same
// program, except that largest_digit claims a bound its digits do not keep.
// Range-based for loops in verified bodies (SPEC.md LOOP-001, LOOP-004,
// STMT-005, STDMODEL-019).
//
// A range-based for over a vector, a string or a span the body names, or over
// an array, is verified as C++ iterates it: one element per position, from the
// first to the last, each read where it owes its bound, the loop variable
// initialized from it or bound to it, and the positions left as the measure
// that makes the loop terminate. An invariant holds at each head, before the
// loop variable is initialized.
//
// None of that runs. Every loop stays in the program as written, so erased,
// this unit must compile to exactly the code `range_for.reference.cpp`
// compiles to, where every contract and loop clause was removed by hand.
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

type Digit = unsigned where (self < 10u);

// Each element by value: every read is within the vector.
verified unsigned sum_vector(const std::vector<unsigned>& v)
    ensures (true)
{
    unsigned total = 0u;
    for (unsigned x : v) {
        total += x;
    }
    return total;
}

// A string's characters.
verified unsigned count_spaces(const std::string& text)
    ensures (true)
{
    unsigned spaces = 0u;
    for (char c : text) {
        if (c == ' ') {
            spaces += 1u;
        }
    }
    return spaces;
}

// A span parameter, each element read under its capability.
verified unsigned sum_span(std::span<const unsigned> s)
    expects (readable(s))
    ensures (true)
{
    unsigned total = 0u;
    for (unsigned x : s) {
        total += x;
    }
    return total;
}

// An array local and a std::array local, each element by const reference.
verified unsigned sum_arrays()
    ensures (true)
{
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

// A refined element supplies its predicate at every read, a refined loop
// variable owes its own at every initialization, and the invariant states
// what the iterations keep.
verified unsigned largest_digit()
    ensures (result < 9u)
{
    std::vector<Digit> digits{3u, 9u, 4u};
    unsigned best = 0u;
    for (Digit d : digits)
        invariant (best < 10u)
    {
        if (d > best) {
            best = d;
        }
    }
    return best;
}

// A write through a reference owes the element type's refinement, and leaves
// the vector's length as it was.
verified std::size_t reset_digits()
    ensures (result == 2ul)
{
    std::vector<Digit> digits{3u, 9u};
    for (Digit& d : digits) {
        d = 1u;
    }
    return digits.size();
}

// A write through a reference to a character of a string the caller owns.
verified void upcase_a(std::string& text)
    ensures (true)
{
    for (char& c : text) {
        if (c == 'a') {
            c = 'A';
        }
    }
}

// `continue` and an early return.
verified unsigned first_over(const std::vector<unsigned>& v, unsigned limit)
    ensures (result == 0u || result > limit)
{
    for (unsigned x : v) {
        if (x <= limit) {
            continue;
        }
        return x;
    }
    return 0u;
}

// `break`.
verified unsigned sum_to_zero(const std::vector<unsigned>& v)
    ensures (true)
{
    unsigned total = 0u;
    for (unsigned x : v) {
        if (x == 0u) {
            break;
        }
        total += x;
    }
    return total;
}

// Nested range-fors, and one inside a while loop.
verified unsigned pairs(const std::vector<unsigned>& v)
    ensures (true)
{
    unsigned total = 0u;
    for (unsigned x : v) {
        for (unsigned y : v) {
            total += x * y;
        }
    }
    unsigned rounds = 0u;
    while (rounds < 2u)
        invariant (rounds <= 2u)
        decreases (2u - rounds)
    {
        for (unsigned x : v) {
            total += x;
        }
        rounds += 1u;
    }
    return total;
}

// An array a parameter designates, and a loop variable wider than its
// elements.
verified unsigned long largest_capped(const unsigned (&values)[3])
    ensures (result <= 9ul)
{
    unsigned long best = 0ul;
    for (unsigned long value : values)
        invariant (best <= 9ul)
    {
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
