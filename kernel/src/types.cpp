#include "cppl/kernel/types.hpp"

#include <cstdint>
#include <limits>
#include <string>

namespace cppl::kernel {

namespace {

bool is_supported_width(const IntType& type) {
    return type.width >= 1 && type.width <= 64;
}

} // namespace

bool is_supported(const IntType& type) {
    return is_supported_width(type) &&
           (type.signedness == Signedness::Signed || type.signedness == Signedness::Unsigned);
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
    return "<unknown-type>";
}

} // namespace cppl::kernel
