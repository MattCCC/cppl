#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/types.hpp"

namespace cppl::vir {

struct Parameter {
    std::string name;
    Type type;

    friend bool operator==(const Parameter&, const Parameter&) = default;
};

// Whether the formal layer may treat this function as a mathematical function.
//
// Pure is recorded only after the purity rules have been checked against the
// resolved body; it is never taken on the strength of the `pure` word alone
// (SPEC.md 13.3).
enum class Purity : std::uint8_t {
    Unknown,
    Pure,
};

struct Function {
    FunctionId id;
    SymbolId symbol;
    std::string qualified_name;
    std::vector<Parameter> parameters;
    Type result;
    Purity purity = Purity::Unknown;

    // The value a single-expression body returns. Absent when the body shape
    // is outside the modeled fragment; such a function cannot be admitted as a
    // formal definition.
    std::optional<Expr> returned_value;

    source::SourceRange range;
};

// A Law: a universally quantified proposition over its parameters.
struct Law {
    LawId id;
    std::string name;
    std::vector<Parameter> parameters;
    Expr proposition;
    source::SourceRange range;
    source::SourceRange proposition_range;
};

struct Module {
    std::vector<Function> functions;
    std::vector<Law> laws;

    [[nodiscard]] const Function* find(const SymbolId& symbol) const;
};

}  // namespace cppl::vir
