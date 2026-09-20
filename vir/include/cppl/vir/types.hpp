#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

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

// The refinement a type names, when it names one (SPEC.md 17, 18).
//
// A refinement is verification-level identity over an ordinary C++ base type. It
// never changes what the type is at runtime, so it is carried beside the base
// type rather than in place of it: `Percentage` and `int` are the same erased
// type and different verification types. Indexed refinements carry the values
// their indices were applied at (SPEC.md 18), so `Index<4>` and `Index<8>` are
// distinct here too.
struct Refinement {
    std::string name;
    std::vector<std::int64_t> arguments;

    friend bool operator==(const Refinement&, const Refinement&) = default;
};

struct Type {
    std::variant<IntType, BoolType, PropositionType> node;

    // Outermost refinement first, so a refinement of a refinement keeps every
    // predicate that applies to the value (SPEC.md 17.5).
    std::vector<Refinement> refinements;
    std::string enumeration;
    std::vector<std::int64_t> enumerators;

    static Type integer(std::uint16_t width, bool is_signed) {
        return Type{IntType{width, is_signed}, {}, {}, {}};
    }
    static Type boolean() {
        return Type{BoolType{}, {}, {}, {}};
    }
    static Type proposition() {
        return Type{PropositionType{}, {}, {}, {}};
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

    [[nodiscard]] bool is_refined() const noexcept {
        return !refinements.empty();
    }

    // The same type with its refinements dropped: what the value is once erased,
    // and what ordinary C++ reasoning is about.
    [[nodiscard]] Type erased() const {
        return Type{node, {}, enumeration, enumerators};
    }

    // Erased C++ identity. Two refinements of one base type are equal here,
    // because they are the same type at runtime; verification-level identity is
    // a separate question, asked where it matters.
    friend bool operator==(const Type& lhs, const Type& rhs) {
        return lhs.node == rhs.node && lhs.enumeration == rhs.enumeration;
    }
};

std::string describe(const Type& type);

} // namespace cppl::vir
