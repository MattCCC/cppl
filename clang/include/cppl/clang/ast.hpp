#pragma once

#include "cppl/source/location.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cppl::clangbridge {

// A narrow, typed mirror of the semantic facts C++L needs from Clang.
//
// Everything here is a *resolved* fact: types are canonical, calls name the
// overload Clang selected, and references name the declaration Clang bound.
// Nothing in this layer is recovered from source text, and no Clang data
// structure or pointer escapes it (ARCHITECTURE.md 12).

enum class TypeKind : std::uint8_t {
    Int,
    Bool,
    Proposition,
    Unsupported,
};

struct Type {
    TypeKind kind = TypeKind::Unsupported;
    std::uint16_t width = 0; // value bits, for Int
    bool is_signed = true;
    std::string spelling;

    friend bool operator==(const Type&, const Type&) = default;
};

enum class BinaryOp : std::uint8_t {
    Add,
    Sub,
    Mul,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And, // C++ `&&` between Boolean operands
    Or,  // C++ `||` between Boolean operands
    Unsupported,
};

struct Expr;

struct ParameterRef {
    std::uint32_t index = 0;
    std::string name;
};

struct IntLiteral {
    std::int64_t value = 0;
};

struct Call {
    std::string callee_usr;
    std::string callee_name;
    std::vector<Expr> arguments;
};

struct Binary {
    BinaryOp op = BinaryOp::Unsupported;
    std::vector<Expr> operands;
};

struct Negation {
    std::vector<Expr> operands;
};
struct FormalEquality {
    Type operand_type;
    std::vector<Expr> operands;
};
struct Universal {
    std::vector<Type> binders;
    std::vector<Expr> body;
};
struct Implication {
    std::vector<Expr> operands;
};
struct Conditional {
    std::vector<Expr> operands;
}; // condition, true return, false return

// The logical version of a local a declaration or an assignment establishes.
// `operands` are the value the version denotes and the rest of the body under
// it. Versions belong to the declaration Clang resolved, never to a spelling,
// and they exist only in the verification model: the runtime statements are
// untouched.
struct LocalVersion {
    std::uint32_t version = 0;
    std::string name;
    std::vector<Expr> operands; // value, body
};

// A read of the version of a local that is current at this point.
struct LocalRef {
    std::uint32_t version = 0;
    std::string name;
};

// A loop, entered with its carried locals at their current versions.
//
// Each local the loop writes is carried: at the head it takes a fresh version,
// `heads`, which denotes whatever value it holds when an iteration begins.
// `operands` are each carried local's value on entry, then each invariant read
// at the head, then what happens from the head on: a conditional on the loop
// condition whose true arm is one iteration and whose false arm is what follows
// the loop. An iteration ends in an Iterate, a return, or a `break` into what
// follows the loop.
struct Loop {
    std::uint32_t loop = 0;
    std::vector<std::uint32_t> heads;
    std::vector<std::string> names;
    std::uint32_t invariants = 0;
    std::vector<Expr> operands; // entry values, invariants, head
};

// The end of one iteration of `loop`: the value each carried local holds when
// the next iteration begins, in the loop's carried order.
struct Iterate {
    std::uint32_t loop = 0;
    std::vector<Expr> operands;
};

// A construct Clang resolved but C++L does not model. Carrying the reason
// keeps the failure explainable instead of silently dropping the expression.
struct Unsupported {
    std::string reason;
};

struct Expr {
    Type type;
    source::SourceLocation location;
    std::variant<ParameterRef, IntLiteral, Call, Binary, Negation, Conditional, LocalVersion, LocalRef, Loop, Iterate,
                 FormalEquality, Universal, Implication, Unsupported>
        node;
};

struct Parameter {
    std::string name;
    Type type;
};

struct Function {
    std::string usr;
    std::string name;
    std::string qualified_name;
    std::vector<Parameter> parameters;
    Type result;
    source::SourceLocation location;
    bool has_body = false;
    std::size_t analysis_offset = 0;

    // A resolved return expression or a finite conditional return tree.
    std::optional<Expr> returned_value;

    // Why the body could not be reduced to a returned expression, when it
    // could not. Exactly one of returned_value / body_rejection is set for a
    // function that has a body.
    std::optional<std::string> body_rejection;

    // The generated invariant declarations the body lowering attached to a
    // loop. Every one the projector emitted for this function must be here.
    std::vector<std::string> loop_invariants;
};

enum class Severity : std::uint8_t {
    Note,
    Warning,
    Error,
    Fatal,
};

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string message;
    source::SourceLocation location;
};

struct TranslationUnit {
    std::vector<Function> functions;
    std::vector<Diagnostic> diagnostics;
    bool has_errors = false;

    [[nodiscard]] const Function* find_by_usr(std::string_view usr) const;
    [[nodiscard]] const Function* find_by_name(std::string_view name) const;
    [[nodiscard]] const Function* find_at_offset(std::size_t offset) const;
};

} // namespace cppl::clangbridge
