#pragma once

// What the files that generate proof obligations share: the lowering of
// VIR into core terms, a proof body under construction, and the steps that
// build evidence for it.

#include "contradiction.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/types.hpp"
#include "lowering.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations::detail::generation {

using detail::arithmetic_fact;

using detail::Failure;

using detail::Standing;

using detail::Unestablished;

using detail::DefinitionMap;

// Defined in generate_terms.cpp.
std::unexpected<Failure> fail(std::string reason, const source::SourceLocation& location);

std::optional<kernel::Type> lower_type(const vir::Type& type);

// Lowers a VIR value expression into a core term.
//
// The core is total: every primitive it offers is defined on every input. A C++
// operator may be lowered onto one only when the C++ operator is equally total.
// Where it is not, the lowering refuses rather than pretending (SPEC.md 29, 31).
class TermLowering {
  public:
    TermLowering(const DefinitionMap& definitions, std::size_t parameter_count,
                 const detail::CallBindings* calls = nullptr, const detail::VersionBindings* versions = nullptr,
                 const detail::OpaqueBindings* opaque = nullptr)
        : definitions_(definitions),
          parameter_count_(parameter_count),
          calls_(calls),
          versions_(versions != nullptr ? *versions : detail::VersionBindings{}),
          opaque_(opaque) {}

    [[nodiscard]] std::expected<kernel::Term, Failure> lower(const vir::Expr& expr);

  private:
    const DefinitionMap& definitions_;
    std::size_t parameter_count_;
    const detail::CallBindings* calls_;
    detail::VersionBindings versions_;
    const detail::OpaqueBindings* opaque_;
    std::uint32_t replay_bound_ = std::numeric_limits<std::uint32_t>::max();
    std::size_t nodes_ = 0;
};

// Defined in generate_propositions.cpp.
std::expected<kernel::Proposition, Failure> lower_proposition(const vir::Expr& expression, const Program& program,
                                                              const DefinitionMap& definitions,
                                                              std::size_t parameter_count);

std::optional<kernel::Proposition> quantify_over(const Program& program, const std::vector<vir::Parameter>& parameters,
                                                 kernel::Proposition body, std::string& unrepresented);

// Defined in generate_identity.cpp.
ObligationId identify(const kernel::Context& context, const std::string& law_name, const kernel::Proposition& goal);

inline void report(diagnostics::Engine& engine, diagnostics::Category category, const source::SourceLocation& location,
                   std::string message, std::string note = {}) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = category;
    diagnostic.message = std::move(message);
    diagnostic.location = location;
    if (!note.empty()) {
        diagnostic.notes.push_back(diagnostics::Note{std::move(note), location});
    }
    engine.report(std::move(diagnostic));
}

// Defined in generate_evidence.cpp.
std::optional<kernel::Proposition> claimed_proposition(const vir::Proof& proof, const Obligation& obligation,
                                                       const DefinitionMap& definitions, diagnostics::Engine& engine);

// The evidence a referenced proof supplies once it is instantiated.
//
// Each argument is one universal elimination. The proof term records the
// proposition it eliminates from so the kernel can check that step itself; the
// proposition tracked alongside is this layer's own account of where the
// elimination has got to, and the kernel derives it again independently.
struct Instantiation {
    kernel::Proposition proposition;
    kernel::ProofTerm term;

    // Whether an instantiation argument mentions a variable. Such evidence
    // means something only underneath the binders it was stated in, so it
    // cannot stand where those binders have not been introduced.
    bool open = false;
};

// Defined in generate_evidence.cpp.
std::optional<Instantiation> instantiate_evidence(const vir::Proof& proof, const std::string& evidence,
                                                  Instantiation state, const std::vector<vir::Expr>& arguments,
                                                  std::size_t depth, const DefinitionMap& definitions,
                                                  diagnostics::Engine& engine);

const kernel::Proposition* under_quantifiers(const kernel::Proposition& goal, std::vector<kernel::Type>& binders);

std::optional<kernel::Proposition> make_rewrite_context(const kernel::Proposition& goal, const kernel::Term& target);

kernel::ProofTerm quantify(const std::vector<kernel::Type>& binders, kernel::ProofTerm term);

bool convertible_equality(const kernel::Context& context, const kernel::Proposition& available,
                          const kernel::Proposition& goal);

kernel::ProofTerm convert_equality(const kernel::Proposition& available, const kernel::Proposition& goal,
                                   kernel::ProofTerm evidence);

std::optional<std::size_t> premises_before_the_goal(const kernel::Context& context,
                                                    const kernel::Proposition& available,
                                                    const kernel::Proposition& goal, bool exact, std::string& reason);

// A proof body under lowering: the evidence already built for the proofs it
// names, the premises standing in scope, and how far through the statement
// sequence the lowering has got.
//
// Statements are read once, in written order. A statement that leaves a goal
// behind is followed by the statements that close it, so the sequence is walked
// exactly as it was written and is never searched.
struct Body {
    const vir::Proof& proof;
    const kernel::Context& context;
    // The unit's stated refinements, so a proposition written in the body
    // quantifies as the one it is compared with does.
    const Program& program;
    const DefinitionMap& definitions;
    const std::vector<WrittenProof>& built;
    const std::map<std::uint32_t, std::size_t>& built_index;
    std::vector<std::pair<std::uint32_t, kernel::Proposition>> assumptions;
    std::uint32_t assumed = 0;
    std::size_t cursor = 0;

    // How many binders enclose the goal being proved. A goal states a
    // proposition that may quantify over binders of its own, so this is not the
    // proof's parameter count: it is what every term written here is stated
    // underneath, and what keeps a name in a statement denoting the same
    // variable however deeply the goal nests (SPEC.md 8).
    std::size_t depth = 0;
    const std::vector<vir::ProofStep>* steps = &proof.steps;
    source::SourceLocation body_location = proof.range.begin;

    // The types of those binders, outermost first, so `depth` is always their
    // count. A claim made inside the proof but checked on its own, an omitted
    // case (SPEC.md CASE-016), is closed over them.
    std::vector<kernel::Type> binders = {};

    // The omitted cases this body has established, each an obligation of its
    // own. They are submitted with the proof only once the proof is admitted,
    // so a refused proof leaves none behind.
    std::vector<Obligation>* omissions = nullptr;

    // The trusted laws the whole proof rests on, supposed outside everything
    // else in it, the first outermost (SPEC.md TRUSTED-006). They are not
    // standing premises: `assume` cannot name one and `contradiction` does not
    // reason from one unless a statement names it as evidence (TRUSTED-008).
    const std::vector<TrustedPremise>* trusted = nullptr;

    // Where `induction x;` asks for evidence for each of its cases (SPEC.md
    // INDUCT-005). Absent, the short form is refused rather than guessed.
    const CaseAutomation* automation = nullptr;
};

// Introduces binders in front of everything a body stands under, for as long as
// this lives: its depth, its binder types, and every premise already standing,
// which the kernel sees shifted past the new binders (check.cpp). Every premise
// in `Body::assumptions` is therefore stated at `Body::depth`, which is what
// lets a statement use any of them - and what lets `contradiction` state all of
// them as facts - without knowing where each was assumed.
class Underneath {
  public:
    Underneath(Body& body, const std::vector<kernel::Type>& binders)
        : body_(body),
          depth_(body.depth),
          count_(body.binders.size()),
          assumptions_(body.assumptions) {
        const auto added = static_cast<std::uint32_t>(binders.size());
        body.depth += binders.size();
        body.binders.insert(body.binders.end(), binders.begin(), binders.end());
        for (auto& assumption : body.assumptions) {
            assumption.second = kernel::shift(assumption.second, added);
        }
    }
    // Only ever shrinks: erasing the binders it added cannot allocate, where
    // resize() has a growing path that can throw out of a destructor.
    ~Underneath() {
        body_.depth = depth_;
        body_.binders.erase(body_.binders.begin() + static_cast<std::ptrdiff_t>(count_), body_.binders.end());
        body_.assumptions = std::move(assumptions_);
    }
    Underneath(const Underneath&) = delete;
    Underneath& operator=(const Underneath&) = delete;
    Underneath(Underneath&&) = delete;
    Underneath& operator=(Underneath&&) = delete;

  private:
    Body& body_;
    std::size_t depth_;
    std::size_t count_;
    std::vector<std::pair<std::uint32_t, kernel::Proposition>> assumptions_;
};

std::optional<kernel::ProofTerm> prove(Body& body, const kernel::Proposition& goal, diagnostics::Engine& engine);

// Defined in generate_proofs.cpp.
std::optional<Instantiation> named_evidence(const Body& body, const vir::ProofStep& step,
                                            const vir::Reference& reference, diagnostics::Engine& engine);

// Defined in generate_cases.cpp.
void record_omission(const Body& body, const vir::CaseArm& arm, const vir::Reference& named,
                     const kernel::ProofTerm& absurd);

// `contradiction e;` closes the goal from evidence that the context cannot
// occur (GRAMMAR.md 5.6, SPEC.md CASE-011), and `omit label by contradiction e;`
// accounts for an omitted case the same way (GRAMMAR.md 5.7, CASE-004).
//
// The contradiction is between the named evidence and every premise standing
// here, which for an omitted case includes that case's own discriminator
// (CASE-013). It is established on its own, before the goal is looked at: the
// premises are refuted into `False`, which no goal takes part in, and only then
// is the goal closed from that by falsity elimination, whatever its shape. A
// goal that merely follows from the premises therefore establishes nothing
// here, so neither form can close a case whose goal happened to be provable
// while claiming the case cannot occur.
//
// The refutation is linear arithmetic whose certificate the kernel checks
// against constraints it states itself (CASE-014), and a context not shown
// contradictory is an ordinary unproven claim (CASE-005, CASE-015), never an
// impossibility.
inline std::optional<kernel::ProofTerm> prove_contradiction(Body& body, const vir::ProofStep& step,
                                                            const vir::ContradictionStep& contradiction,
                                                            const kernel::Proposition& goal,
                                                            diagnostics::Engine& engine,
                                                            const vir::CaseArm* omitted = nullptr) {
    // The goal's own quantifiers come first, as for every statement that names
    // evidence, because an argument may mention them. What stands beneath them
    // is closed whatever it is, so only the binders are kept.
    std::vector<kernel::Type> binders;
    under_quantifiers(goal, binders);
    const Underneath introduced(body, binders);

    const std::string& name = contradiction.evidence.name;
    std::optional<Instantiation> evidence = named_evidence(body, step, contradiction.evidence, engine);
    if (!evidence.has_value()) {
        return std::nullopt;
    }

    std::optional<Instantiation> instantiated = instantiate_evidence(
        body.proof, name, std::move(*evidence), contradiction.arguments, body.depth, body.definitions, engine);
    if (!instantiated.has_value()) {
        return std::nullopt;
    }

    if (!std::holds_alternative<kernel::Eq>(instantiated->proposition.node)) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + name + "' does not establish an equality, so it cannot state a contradiction",
               "it establishes " + kernel::describe(instantiated->proposition));
        return std::nullopt;
    }
    if (!arithmetic_fact(body.context, instantiated->proposition)) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + name +
                   "' establishes an equality linear arithmetic cannot state, so it cannot state a "
                   "contradiction",
               "it establishes " + kernel::describe(instantiated->proposition));
        return std::nullopt;
    }

    // Every standing premise, from the innermost out, so an omitted case's
    // discriminator is never the one left out.
    const kernel::Proposition established = instantiated->proposition;
    std::vector<Standing> standing;
    standing.reserve(body.assumptions.size());
    for (std::size_t index = body.assumptions.size(); index > 0; --index) {
        standing.push_back(Standing{body.assumptions[index - 1].second,
                                    kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{
                                        static_cast<std::uint32_t>(body.assumptions.size() - index)})});
    }

    std::expected<kernel::ProofTerm, Unestablished> absurd = detail::refute(
        body.context,
        kernel::ArithmeticFact{established, kernel::Box<kernel::ProofTerm>{std::move(instantiated->term)}}, standing);
    if (!absurd.has_value()) {
        if (absurd.error().kind == Unestablished::Kind::Unreadable) {
            report(engine, diagnostics::Category::ProofFailure, step.location,
                   "the premises standing here cannot be stated as linear arithmetic, so no contradiction can be "
                   "read from them",
                   absurd.error().detail);
        } else if (omitted != nullptr) {
            report(engine, diagnostics::Category::ProofFailure, omitted->location,
                   "omitted case '" + omitted->label + "' is not shown to be impossible",
                   "'" + name + "' establishes " + kernel::describe(established) +
                       ", and no contradiction with the premises standing in that case, including its own "
                       "discriminator, was found");
        } else {
            report(engine, diagnostics::Category::ProofFailure, step.location,
                   "'" + name + "' does not state a contradiction",
                   "it establishes " + kernel::describe(established) +
                       ", and no contradiction with the premises standing here was found");
        }
        return std::nullopt;
    }

    if (omitted != nullptr && body.omissions != nullptr) {
        record_omission(body, *omitted, contradiction.evidence, *absurd);
    }

    return quantify(binders, kernel::ProofTerm::falsity_elimination(std::move(*absurd)));
}

// Defined in generate_cases.cpp.
std::optional<kernel::ProofTerm> prove_cases(Body& body, const vir::ProofStep& step, const vir::CasesStep& cases,
                                             const kernel::Proposition& goal, diagnostics::Engine& engine);

std::optional<kernel::ProofTerm> prove_induction(Body& body, const vir::ProofStep& step,
                                                 const vir::InductionStep& induction, const kernel::Proposition& goal,
                                                 diagnostics::Engine& engine);

void discharge_path_claims(Program& program, diagnostics::Engine& engine);

} // namespace cppl::obligations::detail::generation
