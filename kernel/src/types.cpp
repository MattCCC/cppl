#include "cppl/kernel/types.hpp"

#include <cstdint>
#include <limits>
#include <string>

namespace cppl::kernel {

namespace {

bool is_supported_width(const IntType& type) {
    return type.width >= 1 && type.width <= 64;
}

using Unsigned = unsigned __int128;

// The absolute value of `value` as a magnitude, correct for the least Wide too.
Unsigned magnitude(Wide value) {
    const auto bits = static_cast<Unsigned>(value);
    return value < 0 ? ~bits + 1 : bits;
}

// Restoring long division on magnitudes, one bit at a time above 64 bits.
void divide_magnitudes(Unsigned numerator, Unsigned denominator, Unsigned& quotient, Unsigned& rest) {
    if ((numerator >> 64) == 0 && (denominator >> 64) == 0) {
        const auto low_numerator = static_cast<std::uint64_t>(numerator);
        const auto low_denominator = static_cast<std::uint64_t>(denominator);
        quotient = low_numerator / low_denominator;
        rest = low_numerator % low_denominator;
        return;
    }

    quotient = 0;
    rest = 0;
    for (int bit = 127; bit >= 0; --bit) {
        rest = (rest << 1) | ((numerator >> bit) & 1);
        if (rest >= denominator) {
            rest -= denominator;
            quotient |= Unsigned{1} << bit;
        }
    }
}

} // namespace

Wide divide(Wide numerator, Wide denominator) {
    Unsigned quotient = 0;
    Unsigned rest = 0;
    divide_magnitudes(magnitude(numerator), magnitude(denominator), quotient, rest);
    const bool negative = (numerator < 0) != (denominator < 0);
    return static_cast<Wide>(negative ? ~quotient + 1 : quotient);
}

Wide remainder(Wide numerator, Wide denominator) {
    Unsigned quotient = 0;
    Unsigned rest = 0;
    divide_magnitudes(magnitude(numerator), magnitude(denominator), quotient, rest);
    return static_cast<Wide>(numerator < 0 ? ~rest + 1 : rest);
}

bool is_supported(const IntType& type) {
    return is_supported_width(type) &&
           (type.signedness == Signedness::Signed || type.signedness == Signedness::Unsigned);
}

bool is_supported(const Type& type) {
    std::size_t nodes = 0;
    const auto valid = [&](auto&& self, const Type& current, unsigned depth) -> bool {
        if (++nodes > 4096 || depth > 64)
            return false;
        if (current.is_integer())
            return is_supported(current.integer_type());
        const auto& value = std::get<ValueType>(current.node);
        if (value.identity.empty() || value.identity.size() > 4096 || value.projections.size() > 256)
            return false;
        for (const auto& projection : value.projections)
            if (!self(self, projection, depth + 1))
                return false;
        return true;
    };
    return valid(valid, type, 0);
}

std::int64_t minimum_value(const IntType& type) {
    if (!is_supported(type) || type.signedness == Signedness::Unsigned) {
        return 0;
    }
    if (type.width == 64) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return -(std::int64_t{1} << (type.width - 1));
}

std::int64_t maximum_value(const IntType& type) {
    if (!is_supported(type)) {
        return 0;
    }
    if (type.signedness == Signedness::Signed) {
        if (type.width == 64) {
            return std::numeric_limits<std::int64_t>::max();
        }
        return (std::int64_t{1} << (type.width - 1)) - 1;
    }
    // Unsigned values wider than 63 bits are outside the range this core can
    // hold in a literal. They are rejected rather than truncated.
    if (type.width >= 64) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return (std::int64_t{1} << type.width) - 1;
}

bool is_representable(const IntType& type, std::int64_t value) {
    if (!is_supported(type)) {
        return false;
    }
    if (type.signedness == Signedness::Unsigned && type.width >= 64) {
        return value >= 0;
    }
    return value >= minimum_value(type) && value <= maximum_value(type);
}

std::int64_t wrap_into(const IntType& type, std::int64_t value) {
    if (!is_supported(type)) {
        return 0;
    }
    const auto raw = static_cast<std::uint64_t>(value);
    if (type.width == 64) {
        return static_cast<std::int64_t>(raw);
    }

    const std::uint64_t mask = (std::uint64_t{1} << type.width) - 1u;
    const std::uint64_t truncated = raw & mask;
    if (type.signedness == Signedness::Signed) {
        const std::uint64_t sign_bit = std::uint64_t{1} << (type.width - 1);
        if ((truncated & sign_bit) != 0u) {
            return static_cast<std::int64_t>(truncated) - static_cast<std::int64_t>(mask) - std::int64_t{1};
        }
    }
    return static_cast<std::int64_t>(truncated);
}

std::string describe(const IntType& type) {
    std::string text = type.signedness == Signedness::Signed ? "i" : "u";
    text += std::to_string(type.width);
    return text;
}

std::string describe(const Type& type) {
    if (type.is_integer()) {
        return describe(type.integer_type());
    }
    const auto& value = std::get<ValueType>(type.node);
    std::string result = "value[" + std::to_string(value.identity.size()) + ":" + value.identity + "]{";
    for (const auto& projection : value.projections)
        result += describe(projection) + ";";
    return result + "}";
}

} // namespace cppl::kernel
