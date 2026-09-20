#pragma once

#include <string_view>

// The vocabulary of case labels, shared by the generic parser, the projector and
// the generic case engine.
//
// A case label is written in one of two ways, and which one it is belongs to the
// representation, not to the syntax:
//
//   Expression  the label is an ordinary C++ expression naming a state, so
//               Clang resolves it and a provider says which case it denotes.
//               A scoped enumerator, `State::idle`, is one.
//   Keyword     the label is a name the representation reserves for a state
//               that has no C++ expression, so it is resolved by spelling
//               against the provider's own labels. `unnamed` is one.
//
// This header is deliberately free of any dependency: the parser must classify a
// label before any semantic information exists, and the generic engine must
// agree with it afterwards. Adding a representation that reserves a label adds
// it here, beside the provider that defines it.
namespace cppl::decomposition {

enum class LabelKind : std::uint8_t {
    Expression,
    Keyword,
};

// The labels representations reserve. Every one belongs to a provider and is
// documented with it; the parser only needs to know that they are not
// expressions.
inline constexpr std::string_view kReservedLabels[] = {
    // Scoped enumerations: the underlying values equal to no enumerator.
    "unnamed",
};

[[nodiscard]] constexpr LabelKind label_kind(std::string_view label) {
    for (const std::string_view reserved : kReservedLabels) {
        if (label == reserved) {
            return LabelKind::Keyword;
        }
    }
    return LabelKind::Expression;
}

} // namespace cppl::decomposition
