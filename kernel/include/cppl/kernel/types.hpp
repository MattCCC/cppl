#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace cppl::kernel {

enum class Signedness : std::uint8_t { Signed, Unsigned };

// Integers wide enough for everything the arithmetic procedures meet: values,
// coefficients and wrap multiples of 64-bit types, and their products with
// certificate multipliers. Every operation on them is overflow-checked, and an
// overflow rejects rather than wraps.
using Wide = __int128;

// A machine integer type: exactly `width` value bits with the stated
// signedness. The formal core never exchanges these for unbounded mathematical
// integers (SPEC.md 29 "Machine arithmetic").
struct IntType {
    std::uint16_t width = 0;
    Signedness signedness = Signedness::Signed;

    friend bool operator==(const IntType&, const IntType&) = default;
};

// Whether this core can represent the type at all. Widths outside 1..64 bits
// are not supported and are rejected rather than approximated.
bool is_supported(const IntType& type);

// The lowest representable value of the type.
std::int64_t minimum_value(const IntType& type);

// The highest representable value of the type.
std::int64_t maximum_value(const IntType& type);

bool is_representable(const IntType& type, std::int64_t value);

// Two's-complement reduction of `value` into `type`.
std::int64_t wrap_into(const IntType& type, std::int64_t value);

struct Type {
    std::variant<IntType> node;

    static Type integer(std::uint16_t width, Signedness signedness) {
        return Type{IntType{width, signedness}};
    }

    [[nodiscard]] bool is_integer() const noexcept {
        return std::holds_alternative<IntType>(node);
    }

    // Requires is_integer().
    [[nodiscard]] const IntType& integer_type() const { return std::get<IntType>(node); }

    friend bool operator==(const Type&, const Type&) = default;
};

std::string describe(const IntType& type);
std::string describe(const Type& type);

}  // namespace cppl::kernel
