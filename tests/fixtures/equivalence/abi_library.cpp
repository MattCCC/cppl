// A C++L library whose interface is written with refinement types, contracts
// and proofs (SPEC.md ABI-001, ABI-002, ABI-003, ERASE-010).
//
// Verification metadata is not native ABI. `abi_client.cpp` is ordinary C++
// compiled by Clang alone, and declares this interface with the base types the
// refinements lower to: it links against this object, calls it, passes and
// returns records by value and by reference, and reads the same layout. A
// refinement that reached a mangled name, a calling convention or a record
// layout would make it fail to link or disagree. `abi_library.reference.cpp`
// is this file erased by hand, and must compile to the same code.
#include <cstddef>

type NonNegative = int where (self >= 0);
type Percentage = NonNegative where (self <= 100);
type Small = unsigned where (self < 10u);
type Index(unsigned n) = unsigned where (self < n);

struct Money {
    unsigned cents;
};

type Positive = Money where (self.cents > 0u);

// Larger than two registers, so it is passed and returned through memory.
struct Reading {
    Percentage level;
    NonNegative count;
    Small digits[3];
    bool flagged;
};

namespace ledger {

pure unsigned twice(unsigned x) {
    return x + x;
}

law twice_is_a_sum(unsigned x)
    proves (twice(x) == x + x);

verified int level_of(Reading reading)
    ensures (result >= 0)
{
    return reading.level;
}

verified int level_through(const Reading& reading)
    ensures (result >= 0)
{
    return reading.level;
}

verified Percentage clamp_percentage(int x)
    ensures (result >= 0)
{
    if (x < 0) {
        return 0;
    }
    if (x > 100) {
        return 100;
    }
    return x;
}

verified unsigned cents(Positive p)
    ensures (result > 0u)
{
    return p.cents;
}

// Ordinary C++ in a C++L unit keeps its ABI too: a record returned by value.
Money refund(Money paid) {
    return Money{paid.cents + 1u};
}

verified Small digit(unsigned x)
    expects (x < 10u)
{
    return x;
}

// A specialization's mangled name carries its return type as the template
// declares it. The refined `Index<N>` is `unsigned` there, so the name is the
// one an ordinary `unsigned` template gets; the reference comparison checks it.
template <unsigned N>
verified Index<N> make_index(unsigned x)
    expects (x < N)
{
    return x;
}

verified unsigned index_of(unsigned x)
    expects (x < 4u)
    ensures (result < 4u)
{
    return make_index<4u>(x);
}

} // namespace ledger

// C linkage names no parameter type, and the refined one is its base.
extern "C" unsigned small_as_c(Small s) {
    return s;
}

// This side's own view of the layout, which the client compares with its own.
std::size_t reading_size() {
    return sizeof(Reading);
}

std::size_t reading_alignment() {
    return alignof(Reading);
}

std::size_t reading_count_offset() {
    return offsetof(Reading, count);
}

std::size_t reading_digits_offset() {
    return offsetof(Reading, digits);
}

std::size_t reading_flagged_offset() {
    return offsetof(Reading, flagged);
}

std::size_t positive_size() {
    return sizeof(Positive);
}
