#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace cppl::kernel {

enum class Signedness : std::uint8_t { Signed, Unsigned };

// Integers wide enough for everything the arithmetic procedures meet: values,
// coefficients and wrap multiples of 64-bit types, and their products with
// certificate multipliers. Every operation on them is overflow-checked, and an
// overflow rejects rather than wraps.
using Wide = __int128;

// Truncating division and remainder of `Wide`, computed in 64-bit pieces.
//
// Targets whose compiler runtime is not linked in (clang targeting the MSVC ABI
// on Windows) have no 128-bit division helper, so `Wide` values are never
// divided with `/` or `%` directly. `denominator` must not be zero.
[[nodiscard]] Wide divide(Wide numerator, Wide denominator);
[[nodiscard]] Wide remainder(Wide numerator, Wide denominator);

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
//
// These are `Wide` because a 64-bit unsigned type's greatest value, 2^64-1,
// does not fit an `int64_t`. Returning it narrowed would make the core's idea
// of a type's range disagree with the machine's.
Wide minimum_value(const IntType& type);

// The highest representable value of the type.
Wide maximum_value(const IntType& type);

bool is_representable(const IntType& type, Wide value);

// Two's-complement reduction of `value` into `type`.
Wide wrap_into(const IntType& type, Wide value);

struct Type;

// An abstract value with a finite signature of total logical observations.
// Identity is nominal; the signature is also part of type identity. Neither
// storage nor a C++ representation is exposed to the kernel.
struct ValueType {
    std::string identity;
    std::vector<Type> projections;

    friend bool operator==(const ValueType&, const ValueType&) = default;
};

struct Type {
    std::variant<IntType, ValueType> node;

    static Type integer(std::uint16_t width, Signedness signedness) {
        return Type{IntType{width, signedness}};
    }

    static Type value(std::string identity, std::vector<Type> projections = {}) {
        return Type{ValueType{std::move(identity), std::move(projections)}};
    }

    [[nodiscard]] bool is_value() const noexcept {
        return std::holds_alternative<ValueType>(node);
    }

    [[nodiscard]] bool is_integer() const noexcept {
        return std::holds_alternative<IntType>(node);
    }

    // Requires is_integer().
    [[nodiscard]] const IntType& integer_type() const {
        return std::get<IntType>(node);
    }

    friend bool operator==(const Type&, const Type&) = default;
};

// Includes structural bounds: malformed recursive signatures fail closed.
bool is_supported(const Type& type);

// The decimal form of a `Wide`. `std::to_string` has no 128-bit overload, so
// values outside `int64_t` would otherwise be unprintable or truncated.
std::string describe(Wide value);

std::string describe(const IntType& type);
std::string describe(const Type& type);

} // namespace cppl::kernel
