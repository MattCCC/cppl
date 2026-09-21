#pragma once

#include <cstdint>

namespace cppl::source {

// Resolved C++ parameter binding, independent of its logical value type.
enum class ParameterPassing : std::uint8_t { Value, ConstReference, MutableReference, RvalueReference };

constexpr bool aliases_storage(ParameterPassing passing) {
    return passing != ParameterPassing::Value;
}

constexpr bool may_write(ParameterPassing passing) {
    return passing == ParameterPassing::MutableReference || passing == ParameterPassing::RvalueReference;
}

} // namespace cppl::source
