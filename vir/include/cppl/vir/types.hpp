#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace cppl::vir {

// A machine integer type as Clang laid it out for the selected target. The
// width is the target's width, not an assumption (SPEC.md 29).
struct IntType {
    std::uint16_t width = 0;
    bool is_signed = true;

    friend bool operator==(const IntType&, const IntType&) = default;
};

struct BoolType {
    friend bool operator==(const BoolType&, const BoolType&) = default;
};

struct PropositionType {
    friend bool operator==(const PropositionType&, const PropositionType&) = default;
};

struct Type {
    std::variant<IntType, BoolType, PropositionType> node;

    static Type integer(std::uint16_t width, bool is_signed) {
        return Type{IntType{width, is_signed}};
    }
    static Type boolean() {
        return Type{BoolType{}};
    }
    static Type proposition() {
        return Type{PropositionType{}};
    }
    [[nodiscard]] bool is_proposition() const noexcept {
        return std::holds_alternative<PropositionType>(node);
    }

    [[nodiscard]] bool is_integer() const noexcept {
        return std::holds_alternative<IntType>(node);
    }
    [[nodiscard]] bool is_boolean() const noexcept {
        return std::holds_alternative<BoolType>(node);
    }

    // Requires is_integer().
    [[nodiscard]] const IntType& integer_type() const {
        return std::get<IntType>(node);
    }

    friend bool operator==(const Type&, const Type&) = default;
};

std::string describe(const Type& type);

} // namespace cppl::vir
