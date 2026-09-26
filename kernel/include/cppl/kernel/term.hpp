#pragma once

#include "cppl/kernel/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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
    AddWrap, // two's-complement wrapping addition
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Not,
    Select,
    SubWrap, // two's-complement wrapping subtraction
    MulWrap, // two's-complement wrapping multiplication

    // Representability (RFC 0019). One when the integer sum, difference or
    // product of the operands' values, computed without any bound, is a value
    // of the type; zero otherwise. These are what a signed C++ operation owes
    // before its result is the ring operation's (SPEC.md ARITH-006,
    // DEFINEDBEHAVIOR-001): where one holds, the wrapping primitive wrapped
    // nothing.
    AddFits,
    SubFits,
    MulFits,

    // Truncating division and its remainder (RFC 0019), made total: the
    // quotient rounds toward zero, a quotient the type cannot hold (the least
    // signed value over -1) wraps, `x / 0` is 0, and the remainder is always
    // `x` minus the exact quotient times the divisor, so `x % 0` is `x`. The
    // cases C++ leaves undefined are the lowering's obligations, never these
    // definitions (SPEC.md ARITH-007, DEFINEDBEHAVIOR-002, DEFINEDBEHAVIOR-003).
    Quotient,
    Remainder,

    // The value of the one argument, of any integer type, reduced into this
    // type by two's complement (RFC 0019). That is C++'s conversion to an
    // unsigned type exactly; to a signed type it is C++20's, and the lowering
    // owes representability wherever the value may not fit (SPEC.md
    // ARITH-008). It is not C++'s conversion to `bool`, which the lowering
    // never states with it.
    Convert,
};

std::string describe(PrimOp op);
bool is_comparison(PrimOp op);

// The operations of the ring of integers modulo 2^width.
bool is_arithmetic(PrimOp op);

// AddFits, SubFits and MulFits: boolean-valued, of two operands of the type.
bool is_representability(PrimOp op);

// Whether a primitive's value is a boolean whatever type it is stated at.
bool yields_boolean(PrimOp op);

// The number of arguments a primitive takes.
std::size_t arity(PrimOp op);
inline constexpr IntType kBoolean{1, Signedness::Unsigned};

struct Term;

struct Var {
    VarIndex index;

    friend bool operator==(const Var&, const Var&) = default;
};

// `value` is `Wide` so that every value of every supported type is denotable:
// a u64 literal ranges up to 2^64-1, which an `int64_t` cannot hold. A literal
// outside its type's range is malformed and is rejected when it is typed.
struct Literal {
    IntType type;
    Wide value = 0;

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

struct Projection {
    Type domain;
    std::uint32_t index = 0;
    std::vector<Term> arguments; // exactly one, of domain type

    friend bool operator==(const Projection&, const Projection&) = default;
};

// One component of an indexed domain, selected at a term (FOUNDATIONS.md 45).
//
// This is the term-indexed sibling of `Projection`, not a replacement for it:
// a projection selects from a heterogeneous signature, so its result type
// depends on a constant position, while an element selects from a homogeneous
// domain and so has one result type whatever the index denotes.
//
// The observation is total. Forming it proves nothing about the index, and in
// particular does not prove `index < extent`: that obligation belongs to the
// C++ subscript and is discharged separately (SPEC.md STORAGE-005).
struct Element {
    Type domain;
    std::vector<Term> arguments; // exactly two: subject of domain type, then index

    friend bool operator==(const Element&, const Element&) = default;
};

struct Term {
    std::variant<Var, Literal, Call, Prim, Projection, Element> node;

    static Term variable(VarIndex index) {
        return Term{Var{index}};
    }
    static Term literal(IntType type, Wide value) {
        return Term{Literal{type, value}};
    }
    static Term call(DefId callee, std::vector<Term> arguments) {
        return Term{Call{callee, std::move(arguments)}};
    }
    static Term primitive(PrimOp op, IntType type, std::vector<Term> arguments) {
        return Term{Prim{op, type, std::move(arguments)}};
    }
    static Term project(Type domain, std::uint32_t index, Term subject) {
        return Term{Projection{std::move(domain), index, {std::move(subject)}}};
    }
    static Term element(Type domain, Term subject, Term index) {
        return Term{Element{std::move(domain), {std::move(subject), std::move(index)}}};
    }

    friend bool operator==(const Term&, const Term&) = default;
};

// A term former is part of the logical TCB: typing, substitution, normalization
// and structural identity must each account for every alternative. A dispatch
// chain ending in a catch-all `else` would silently treat a new alternative as
// the one that `else` handles, which is how an unchecked term becomes a checked
// one (TRUST.md TCB-CORE-001, AGENTS.md 7).
//
// Adding an alternative must therefore fail here first. Update the count only
// together with every site that decides what the new former means.
static_assert(std::variant_size_v<decltype(Term::node)> == 6,
              "a kernel term former was added or removed: review type_of, normalize and describe_with_names in "
              "context.cpp, shift/instantiate in substitution.cpp, compare in arithmetic.cpp, describe in "
              "term.cpp, and the obligation hashing in obligations/generate.cpp");

// The de Bruijn index denoting parameter `position` of a definition declaring
// `parameter_count` parameters. Parameters are bound outermost-first, so
// position 0 is the outermost binder and has the largest index.
VarIndex parameter_reference(std::size_t parameter_count, std::size_t position);

std::string describe(const Term& term);

} // namespace cppl::kernel
