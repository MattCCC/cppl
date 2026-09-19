#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "cppl/kernel/types.hpp"

namespace cppl::kernel {

// de Bruijn index. Var{0} denotes the innermost enclosing binder.
struct VarIndex {
    std::uint32_t value = 0;

    friend auto operator<=>(const VarIndex&, const VarIndex&) = default;
};

// Identity of a definition inside a Context. Ids are assigned by the caller and
// are part of the checked input; they are never derived from addresses.
struct DefId {
    std::uint32_t value = 0;

    friend auto operator<=>(const DefId&, const DefId&) = default;
};

// Machine primitives. Each primitive denotes exactly one total operation on bit
// patterns of a machine integer type. A primitive is not a stand-in for a C++
// operator whose C++ semantics are narrower: lowering a C++ operator onto a
// primitive is the elaborator's responsibility, including any side conditions
// that C++ requires for defined behavior (SPEC.md 29, 31).
enum class PrimOp : std::uint8_t {
    AddWrap,  // two's-complement wrapping addition
};

std::string describe(PrimOp op);

struct Term;

struct Var {
    VarIndex index;

    friend bool operator==(const Var&, const Var&) = default;
};

struct Literal {
    IntType type;
    std::int64_t value = 0;

    friend bool operator==(const Literal&, const Literal&) = default;
};

struct Call {
    DefId callee;
    std::vector<Term> arguments;

    friend bool operator==(const Call&, const Call&) = default;
};

struct Prim {
    PrimOp op;
    IntType type;
    std::vector<Term> arguments;

    friend bool operator==(const Prim&, const Prim&) = default;
};

struct Term {
    std::variant<Var, Literal, Call, Prim> node;

    static Term variable(VarIndex index) { return Term{Var{index}}; }
    static Term literal(IntType type, std::int64_t value) { return Term{Literal{type, value}}; }
    static Term call(DefId callee, std::vector<Term> arguments) {
        return Term{Call{callee, std::move(arguments)}};
    }
    static Term primitive(PrimOp op, IntType type, std::vector<Term> arguments) {
        return Term{Prim{op, type, std::move(arguments)}};
    }

    friend bool operator==(const Term&, const Term&) = default;
};

// The de Bruijn index denoting parameter `position` of a definition declaring
// `parameter_count` parameters. Parameters are bound outermost-first, so
// position 0 is the outermost binder and has the largest index.
VarIndex parameter_reference(std::size_t parameter_count, std::size_t position);

std::string describe(const Term& term);

}  // namespace cppl::kernel
