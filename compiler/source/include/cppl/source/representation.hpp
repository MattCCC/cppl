#pragma once

#include "cppl/source/location.hpp"

#include <cstdint>
#include <string>

namespace cppl::source {
// Classification of canonical Clang declarations, never source type spellings.
enum class RepresentationKind : std::uint8_t {
    None,
    ScopedEnum,
    Pointer,
    Record,
    Array,
    Pair,
    Tuple,
    StdArray,
    Variant,
    Optional,
    Expected
};
struct Component {
    std::string name;
    SourceLocation declaration;
    bool accessible = true;
    friend bool operator==(const Component&, const Component&) = default;
};
} // namespace cppl::source
