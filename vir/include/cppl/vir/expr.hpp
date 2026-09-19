#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "cppl/source/location.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/types.hpp"

namespace cppl::vir {

// Where a VIR node came from in user source. Provenance is carried, never
// reconstructed later from text (ARCHITECTURE.md 41).
struct Provenance {
    source::SourceRange range;

    friend bool operator==(const Provenance&, const Provenance&) = default;
};

// Operations are typed variants, never strings or numeric tags.
enum class BinaryOp : std::uint8_t {
    Add,    // addition, at the operand type's machine semantics
    Equal,  // equality comparison, yielding bool
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

std::string describe(BinaryOp op);

struct Expr;

// Reference to a parameter of the enclosing function or law, by position.
struct ParameterRef {
    std::uint32_t parameter = 0;
    std::string name;

    friend bool operator==(const ParameterRef&, const ParameterRef&) = default;
};

struct IntLiteral {
    std::int64_t value = 0;

    friend bool operator==(const IntLiteral&, const IntLiteral&) = default;
};

struct Call {
    SymbolId callee;
    std::string callee_name;
    std::vector<Expr> arguments;

    friend bool operator==(const Call&, const Call&) = default;
};

struct Binary {
    BinaryOp op = BinaryOp::Add;
    std::vector<Expr> operands;  // exactly two

    friend bool operator==(const Binary&, const Binary&) = default;
};

struct Negation {
    std::vector<Expr> operands;
    friend bool operator==(const Negation&, const Negation&) = default;
};

struct Conditional {
    std::vector<Expr> operands; // condition, true return, false return
    friend bool operator==(const Conditional&, const Conditional&) = default;
};

struct Expr {
    ExprId id;
    Type type;
    Provenance provenance;
    std::variant<ParameterRef, IntLiteral, Call, Binary, Negation, Conditional> node;

    friend bool operator==(const Expr&, const Expr&) = default;
};

std::string describe(const Expr& expr);

}  // namespace cppl::vir
