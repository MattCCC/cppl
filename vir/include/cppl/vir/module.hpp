#pragma once

#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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

// The contract a verified function must satisfy (GRAMMAR.md 6).
//
// `postcondition` is stated over the function's parameters and one more, in
// last position, standing for the value the function returns. That parameter is
// what `result` denotes; it exists only in the specification. The
// preconditions, in source order, conjoin (SPEC.md 11.5).
struct Contract {
    std::vector<Expr> preconditions;
    Expr postcondition;
    source::SourceRange range;
};

struct Function {
    FunctionId id;
    SymbolId symbol;
    std::string qualified_name;
    std::vector<Parameter> parameters;
    Type result;
    Purity purity = Purity::Unknown;
    std::optional<Contract> contract;

    // The returned expression or conditional return tree. Absent when the body shape
    // is outside the modeled fragment; such a function cannot be admitted as a
    // formal definition.
    std::optional<Expr> returned_value;

    source::SourceRange range;
};

// A Law: a universally quantified proposition over its parameters.
//
// A Law with a precondition states an implication. The premise is not a claim
// the Law makes: it is what the conclusion is stated under (GRAMMAR.md 3).
struct Law {
    LawId id;
    std::string name;
    std::vector<Parameter> parameters;
    Expr proposition;
    std::optional<Expr> premise;
    source::SourceRange range;
    source::SourceRange proposition_range;
    source::SourceRange premise_range;
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

// Evidence a step names: a proof declared in this unit, or a premise this proof
// has assumed. Which of the two it is is settled when the name is resolved, and
// is never re-decided from the spelling afterwards.
struct ProofRef {
    ProofId proof;

    friend bool operator==(const ProofRef&, const ProofRef&) = default;
};

struct HypothesisRef {
    std::uint32_t assumption = 0; // which `assume` in this body bound the name

    friend bool operator==(const HypothesisRef&, const HypothesisRef&) = default;
};

struct Reference {
    std::variant<ProofRef, HypothesisRef> node;
    std::string name;

    friend bool operator==(const Reference&, const Reference&) = default;
};

// `exact p;` - p's proposition must be the goal itself.
//
// `arguments` are the terms p is instantiated at before it is compared with the
// goal, one universal elimination each, in written order.
struct ExactStep {
    Reference evidence;
    std::vector<Expr> arguments;

    friend bool operator==(const ExactStep&, const ExactStep&) = default;
};

// `apply p;` - p's conclusion, instantiated at `arguments`, must be applicable
// to the goal. Each premise it carries becomes a goal of its own.
struct ApplyStep {
    Reference evidence;
    std::vector<Expr> arguments;

    friend bool operator==(const ApplyStep&, const ApplyStep&) = default;
};

// `assume h : P;` - names the premise of the goal being proven (GRAMMAR.md
// 5.4). It introduces nothing of its own: the proposition must be the premise
// the goal already supposes.
struct AssumeStep {
    std::string name;
    Expr proposition;

    friend bool operator==(const AssumeStep&, const AssumeStep&) = default;
};

// `rewrite p;` - p's equality, instantiated at `arguments`, transforms the
// goal, and what it leaves is a goal of its own (GRAMMAR.md 5.5).
struct RewriteStep {
    Reference evidence;
    std::vector<Expr> arguments;

    friend bool operator==(const RewriteStep&, const RewriteStep&) = default;
};

struct ProofStep {
    std::variant<ReflexivityStep, ExactStep, ApplyStep, AssumeStep, RewriteStep> node;
    source::SourceLocation location;
};

// A written proof: evidence an author supplied for one Law.
struct Proof {
    ProofId id;
    std::string name;
    LawId law;
    std::vector<Parameter> parameters;
    Expr proposition; // the resolved `proves` clause
    std::vector<ProofStep> steps;
    source::SourceRange range;
};

struct Module {
    std::vector<Function> functions;
    std::vector<Law> laws;
    std::vector<Proof> proofs;

    [[nodiscard]] const Function* find(const SymbolId& symbol) const;
};

} // namespace cppl::vir
