#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
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

// The written proof steps of GRAMMAR.md 5, resolved.
//
// A step is a typed node naming the proof it uses, not a tactic name to be
// interpreted later. What each step means as evidence is decided when it is
// lowered to a kernel proof term; whether that evidence holds is decided by the
// kernel alone.

struct ReflexivityStep {
    friend bool operator==(const ReflexivityStep&, const ReflexivityStep&) = default;
};

// `exact p;` - p's proposition must be the goal itself.
//
// `arguments` are the terms p is instantiated at before it is compared with the
// goal, one universal elimination each, in written order.
struct ExactStep {
    ProofId target;
    std::string target_name;
    std::vector<Expr> arguments;

    friend bool operator==(const ExactStep&, const ExactStep&) = default;
};

// `apply p;` - p's conclusion, instantiated at `arguments`, must be applicable
// to the goal.
struct ApplyStep {
    ProofId target;
    std::string target_name;
    std::vector<Expr> arguments;

    friend bool operator==(const ApplyStep&, const ApplyStep&) = default;
};

struct ProofStep {
    std::variant<ReflexivityStep, ExactStep, ApplyStep> node;
    source::SourceLocation location;
};

// A written proof: evidence an author supplied for one Law.
struct Proof {
    ProofId id;
    std::string name;
    LawId law;
    std::vector<Parameter> parameters;
    Expr proposition;  // the resolved `proves` clause
    ProofStep step;
    source::SourceRange range;
};

struct Module {
    std::vector<Function> functions;
    std::vector<Law> laws;
    std::vector<Proof> proofs;

    [[nodiscard]] const Function* find(const SymbolId& symbol) const;
};

}  // namespace cppl::vir
