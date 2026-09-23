#pragma once

#include "cppl/source/location.hpp"
#include "cppl/source/storage.hpp"
#include "cppl/vir/capability.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cppl::vir {

struct Parameter {
    std::string name;
    Type type;
    source::ParameterPassing passing = source::ParameterPassing::Value;

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
// `capabilities` are the memory propositions an `expects` clause states
// (SPEC.md 12.10). They are kept beside the preconditions rather than among
// them because they are not kernel propositions: the obligation layer supposes
// them as context hypotheses and the kernel never sees them (RFC 0014 §10).
// Putting them in `preconditions` would conjoin them into the goal, which is
// exactly the rejected option.
struct Contract {
    std::vector<Expr> preconditions;
    std::vector<Capability> capabilities;
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

    // An explicit assumption (SPEC.md 27). Its proposition is still stated to
    // the formal core, so it must be expressible and it can be used like any
    // other law; what differs is that nothing discharges it, and its status is
    // TRUSTED rather than PROVEN.
    bool trusted = false;
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

// `contradiction p;` - p's evidence, instantiated at `arguments`, together with
// the premises standing where it is written, cannot all hold; that closes the
// goal whatever its shape (GRAMMAR.md 5.6, SPEC.md CASE-011, CASE-013).
//
// This is a distinct step rather than an `apply` of a false premise because what
// it claims is distinct: that this context cannot occur. Written as an omitted
// case's discharge (`CaseArm::omitted`), that claim becomes an obligation of its
// own, recorded under its own origin so it is never reported as an unreachable
// runtime path (SPEC.md CASE-012, CASE-016).
struct ContradictionStep {
    Reference evidence;
    std::vector<Expr> arguments;

    friend bool operator==(const ContradictionStep&, const ContradictionStep&) = default;
};

struct CaseArm;

// `cases s { ... }` - proof-side reasoning over the states of an ordinary C++
// value (SPEC.md 20). It is not runtime control flow and generates no runtime
// code: every arm proves the enclosing goal under the facts its case supplies.
//
// Nothing here names a representation. The subject's decomposition provider
// says what the cases are; this records only which of them each arm claims to
// prove, so the obligation layer can ask the provider again and check that the
// arms cover the partition it describes.
struct CasesStep {
    Expr subject;
    std::vector<CaseArm> arms;
};

struct ProofStep;
struct ProductStep {
    Expr subject;
    std::vector<ProofStep> steps;
};

struct ProofStep {
    std::variant<ReflexivityStep, ExactStep, ApplyStep, AssumeStep, RewriteStep, ContradictionStep, CasesStep,
                 ProductStep>
        node;
    source::SourceLocation location;
};

struct CaseArm {
    // Which case of the subject's decomposition this arm proves. Absent for the
    // residual case, which is the one the provider does not enumerate.
    std::optional<std::uint32_t> descriptor;

    // The label as the provider spells it, for diagnostics only.
    std::string label;

    // The case was omitted and discharged by contradiction (SPEC.md 20.2
    // CASE-004 clause 2) rather than proven by an arm body. `steps` holds the
    // single contradiction step, checked under this case's discriminator
    // premise; the branch's goal is then closed from that contradiction, never
    // from the goal being provable on its own.
    bool omitted = false;

    std::vector<ProofStep> steps;
    source::SourceLocation location;
};

// A written proof: evidence an author supplied for one Law.
struct Proof {
    ProofId id;
    std::string name;
    std::optional<LawId> law;
    std::vector<Parameter> parameters;
    Expr proposition; // the resolved `proves` clause
    std::vector<ProofStep> steps;
    source::SourceRange range;
};

// A refinement type: an ordinary C++ base type and a predicate its values
// satisfy (SPEC.md 17).
//
// `predicate` is stated over the declaration's indices and one more parameter, in
// last position, standing for the value being refined. That parameter is what
// `self` denotes; it exists only in the specification. The type has no runtime
// representation of its own, so nothing here reaches code generation - what the
// program keeps is the base type (SPEC.md 17.4).
struct RefinementDeclaration {
    std::string name;
    Type base;
    std::vector<Parameter> indices;
    Expr predicate;
    source::SourceRange range;
    source::SourceRange predicate_range;
    std::string identity = {};
};

struct Module {
    std::vector<Function> functions;
    std::vector<Law> laws;
    std::vector<Proof> proofs;
    std::vector<RefinementDeclaration> refinements;

    [[nodiscard]] const Function* find(const SymbolId& symbol) const;

    // The declaration a refinement name stands for, or null when the name is not
    // one this unit declares.
    [[nodiscard]] const RefinementDeclaration* find_refinement(std::string_view name) const;
};

} // namespace cppl::vir
