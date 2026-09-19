#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "cppl/source/location.hpp"

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
    Unsupported,
};

struct Type {
    TypeKind kind = TypeKind::Unsupported;
    std::uint16_t width = 0;  // value bits, for Int
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

struct Negation { std::vector<Expr> operands; };
struct Conditional { std::vector<Expr> operands; }; // condition, true return, false return

// The logical version of a local a declaration or an assignment establishes.
// `operands` are the value the version denotes and the rest of the body under
// it. Versions belong to the declaration Clang resolved, never to a spelling,
// and they exist only in the verification model: the runtime statements are
// untouched.
struct LocalVersion {
    std::uint32_t version = 0;
    std::string name;
    std::vector<Expr> operands;  // value, body
};

// A read of the version of a local that is current at this point.
struct LocalRef {
    std::uint32_t version = 0;
    std::string name;
};

// A construct Clang resolved but C++L does not model. Carrying the reason
// keeps the failure explainable instead of silently dropping the expression.
struct Unsupported {
    std::string reason;
};

struct Expr {
    Type type;
    source::SourceLocation location;
    std::variant<ParameterRef, IntLiteral, Call, Binary, Negation, Conditional, LocalVersion,
                 LocalRef, Unsupported>
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

    // A resolved return expression or a finite conditional return tree.
    std::optional<Expr> returned_value;

    // Why the body could not be reduced to a returned expression, when it
    // could not. Exactly one of returned_value / body_rejection is set for a
    // function that has a body.
    std::optional<std::string> body_rejection;
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
    [[nodiscard]] const Function* find_at(const source::SourceLocation& location) const;
};

}  // namespace cppl::clangbridge
