#pragma once

#include "cppl/source/representation.hpp"

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

struct VoidType {
    friend bool operator==(const VoidType&, const VoidType&) = default;
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
    std::string identity = {};

    friend bool operator==(const Refinement&, const Refinement&) = default;
};

// One enumerator of a scoped enumeration, as Clang resolved it. Enumerators
// that share a value are aliases of one another and name one logical case; they
// are all kept so diagnostics can name whichever one an author wrote.
struct Enumerator {
    std::string name;
    std::int64_t value = 0;

    friend bool operator==(const Enumerator&, const Enumerator&) = default;
};

// The resolved identity of a C++ representation whose proof-visible states a
// decomposition provider models (SPEC.md 20.1, 20.4).
//
// `identity` is Clang's USR, so provider selection is by resolved semantic
// identity and never by spelling: an alias, a qualified name and a template
// specialization that resolve to one declaration share it.
struct Representation {
    std::string identity = {};
    // The qualified name, for diagnostics only.
    std::string name;
    std::vector<Enumerator> enumerators;
    source::RepresentationKind kind = source::RepresentationKind::None;
    std::vector<source::Component> components;
    std::string rejection;

    [[nodiscard]] bool is_known() const noexcept {
        return !identity.empty();
    }

    friend bool operator==(const Representation& lhs, const Representation& rhs) {
        return lhs.identity == rhs.identity;
    }
};

struct Type;
struct ValueType {
    std::vector<Type> projections;
    friend bool operator==(const ValueType&, const ValueType&) = default;
};

struct Type {
    std::variant<IntType, BoolType, PropositionType, ValueType, VoidType> node;

    // Outermost refinement first, so a refinement of a refinement keeps every
    // predicate that applies to the value (SPEC.md 17.5).
    std::vector<Refinement> refinements;

    // The C++ representation this type stands for, when it is one a provider
    // may model. Empty for a plain modeled scalar.
    Representation representation;

    static Type integer(std::uint16_t width, bool is_signed) {
        return Type{IntType{width, is_signed}, {}, {}};
    }
    static Type void_type() {
        return Type{VoidType{}, {}, {}};
    }
    [[nodiscard]] bool is_void() const noexcept {
        return std::holds_alternative<VoidType>(node);
    }
    static Type boolean() {
        return Type{BoolType{}, {}, {}};
    }
    static Type value() {
        return Type{ValueType{}, {}, {}};
    }
    [[nodiscard]] bool is_value() const noexcept {
        return std::holds_alternative<ValueType>(node);
    }
    static Type proposition() {
        return Type{PropositionType{}, {}, {}};
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
        return Type{node, {}, representation};
    }

    // The scalar the representation is carried in, with the representation
    // forgotten. This is what a binder that names the underlying value denotes;
    // it is the same runtime value, reasoned about as a plain machine integer.
    [[nodiscard]] Type underlying() const {
        return Type{node, refinements, {}};
    }

    // Erased C++ identity. Two refinements of one base type are equal here,
    // because they are the same type at runtime; verification-level identity is
    // a separate question, asked where it matters.
    friend bool operator==(const Type& lhs, const Type& rhs) {
        return lhs.node == rhs.node && lhs.representation == rhs.representation;
    }
};

std::string describe(const Type& type);

} // namespace cppl::vir
