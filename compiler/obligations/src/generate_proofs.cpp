// Proving a written proof body: its statements, the premises it supposes,
// and the evidence it names and transports.

#include "cppl/decomposition/decomposition.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/module.hpp"
#include "generate_detail.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations {

using detail::generation::Body;
using detail::generation::instantiate_evidence;
using detail::generation::Instantiation;
using detail::generation::lower_proposition;
using detail::generation::named_evidence;
using detail::generation::prove;
using detail::generation::quantify;
using detail::generation::report;
using detail::generation::under_quantifiers;
using detail::generation::Underneath;

namespace {

// Where a trusted premise stands among the hypotheses in scope. The trusted
// premises are introduced before anything the body assumes, so each counts back
// past every standing premise.
std::optional<kernel::HypothesisIndex> trusted_hypothesis(const Body& body, vir::LawId law) {
    if (body.trusted == nullptr)
        return std::nullopt;
    const auto found =
        std::ranges::find_if(*body.trusted, [law](const TrustedPremise& premise) { return premise.law == law; });
    if (found == body.trusted->end())
        return std::nullopt;
    const auto position = static_cast<std::size_t>(std::distance(body.trusted->begin(), found));
    return kernel::HypothesisIndex{
        static_cast<std::uint32_t>(body.assumptions.size() + (body.trusted->size() - 1 - position))};
}

// `assume h : P;` names the premise the goal supposes. It introduces the
// quantifiers standing in front of that premise, because a premise stated of
// the proof's parameters is only visible underneath them.
//
// Nothing is assumed that the goal did not already suppose: the proposition
// written here is compared with the goal's own premise, and the hypothesis
// exists only because the implication introduction below puts it there.
std::optional<kernel::ProofTerm> suppose(Body& body, const vir::ProofStep& step, const vir::AssumeStep& assumed,
                                         const kernel::Proposition& goal, diagnostics::Engine& engine) {
    const std::uint32_t position = body.assumed++;

    std::vector<kernel::Type> binders;
    const kernel::Proposition* inner = under_quantifiers(goal, binders);
    const std::size_t depth = body.depth + binders.size();

    auto written = lower_proposition(assumed.proposition, body.program, body.definitions, depth);
    if (!written) {
        report(engine, diagnostics::Category::UnsupportedSemantics, step.location,
               "the assumed proposition cannot be stated: " + written.error().reason);
        return std::nullopt;
    }
    // Case premises already stand in the context. Naming one does not create
    // another assumption or consume an implication from the enclosing goal.
    constexpr auto anonymous = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t index = body.assumptions.size(); index > 0; --index) {
        auto& premise = body.assumptions[index - 1];
        if (premise.first == anonymous &&
            kernel::shift(premise.second, static_cast<std::uint32_t>(binders.size())) == *written) {
            premise.first = position;
            auto result = prove(body, goal, engine);
            body.assumptions[index - 1].first = anonymous;
            return result;
        }
    }
    const auto* implication = std::get_if<kernel::Implies>(&inner->node);
    if (implication == nullptr) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + assumed.name + "' has no matching premise to stand for",
               "the goal here is " + kernel::describe(*inner));
        return std::nullopt;
    }

    if (!(*written == *implication->premise)) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + assumed.name + "' does not name the premise this goal supposes",
               "it states " + kernel::describe(*written) + ", and the premise is " +
                   kernel::describe(*implication->premise));
        return std::nullopt;
    }

    std::optional<kernel::ProofTerm> rest;
    {
        // The premise is stated underneath the binders just introduced, so it
        // joins the standing premises after they have been shifted past them.
        const Underneath introduced(body, binders);
        body.assumptions.emplace_back(position, *implication->premise);
        rest = prove(body, *implication->conclusion, engine);
    }
    if (!rest.has_value()) {
        return std::nullopt;
    }

    return quantify(binders, kernel::ProofTerm::implication_introduction(*implication->premise, std::move(*rest)));
}

} // namespace

namespace detail::generation {

// The evidence a statement names, before it is instantiated: a proof this unit
// has already built, or a premise standing in scope.
std::optional<Instantiation> named_evidence(const Body& body, const vir::ProofStep& step,
                                            const vir::Reference& reference, diagnostics::Engine& engine) {
    if (const auto* assumed = std::get_if<vir::HypothesisRef>(&reference.node)) {
        const auto found = std::ranges::find_if(
            body.assumptions, [&assumed](const auto& entry) { return entry.first == assumed->assumption; });
        if (found == body.assumptions.end()) {
            report(engine, diagnostics::Category::ProofFailure, step.location,
                   "the premise '" + reference.name + "' names is not in scope here",
                   "it was assumed for a goal that has already been closed");
            return std::nullopt;
        }

        // A premise is named from the inside out, so its index counts back from
        // the most recently assumed one.
        const auto position = static_cast<std::uint32_t>(
            body.assumptions.size() - 1 - static_cast<std::size_t>(std::distance(body.assumptions.begin(), found)));
        return Instantiation{found->second, kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{position})};
    }

    // Every trusted law this proof rests on was collected from its statements
    // before it was lowered, so a law missing here is a fault in that
    // collection. It is refused rather than left out of what the kernel checks.
    const auto unaccounted = [&](const std::string& law) {
        report(engine, diagnostics::Category::Internal, step.location,
               "trusted law '" + law + "' is used by proof '" + body.proof.name + "' but is not among its premises");
        return std::nullopt;
    };

    if (const auto* trusted = std::get_if<vir::TrustedLawRef>(&reference.node)) {
        const std::optional<kernel::HypothesisIndex> index = trusted_hypothesis(body, trusted->law);
        if (!index.has_value()) {
            return unaccounted(reference.name);
        }
        const auto& premise = *std::ranges::find_if(
            *body.trusted, [&trusted](const TrustedPremise& candidate) { return candidate.law == trusted->law; });
        return Instantiation{premise.proposition, kernel::ProofTerm::hypothesis(*index)};
    }

    const auto source = body.built_index.find(std::get<vir::ProofRef>(reference.node).proof.value);
    const WrittenProof& used = body.built[source->second];

    // A proof established relative to trusted laws is used relative to the
    // same ones: each premise it supposes is discharged by this body's own
    // supposition of that law, so the dependency carries over and is never
    // dropped on the way (TRUST.md TCB-TRUST-003).
    kernel::ProofTerm term = used.term;
    kernel::Proposition current = relative_to(used.assumptions, used.goal);
    for (const TrustedPremise& premise : used.assumptions) {
        const std::optional<kernel::HypothesisIndex> index = trusted_hypothesis(body, premise.law);
        if (!index.has_value()) {
            return unaccounted(premise.name);
        }
        kernel::Proposition conclusion = *std::get<kernel::Implies>(current.node).conclusion;
        term = kernel::ProofTerm::implication_elimination(std::move(current), std::move(term),
                                                          kernel::ProofTerm::hypothesis(*index));
        current = std::move(conclusion);
    }
    return Instantiation{used.goal, std::move(term)};
}

} // namespace detail::generation

namespace {

// `rewrite e;` transforms the goal with an equality and leaves what it
// transformed it into to prove.
//
// The quantifiers the goal leads with are introduced first, because evidence
// stated of the proof's parameters only reaches the goal underneath them. The
// equality is not asserted here: it is evidence like any other, and the kernel
// checks it along with the context this builds.
std::optional<kernel::ProofTerm> transport(Body& body, const vir::ProofStep& step, const vir::RewriteStep& rewritten,
                                           const kernel::Proposition& goal, diagnostics::Engine& engine) {
    std::vector<kernel::Type> binders;
    const kernel::Proposition* inner = under_quantifiers(goal, binders);
    const std::size_t depth = body.depth + binders.size();

    std::optional<Instantiation> evidence = named_evidence(body, step, rewritten.evidence, engine);
    if (!evidence.has_value()) {
        return std::nullopt;
    }

    std::optional<Instantiation> instantiated =
        instantiate_evidence(body.proof, rewritten.evidence.name, std::move(*evidence), rewritten.arguments, depth,
                             body.definitions, engine);
    if (!instantiated.has_value()) {
        return std::nullopt;
    }

    const auto* equality = std::get_if<kernel::Eq>(&instantiated->proposition.node);
    if (equality == nullptr) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + rewritten.evidence.name +
                   "' does not establish an equality, so there is "
                   "nothing for it to rewrite",
               "it establishes " + kernel::describe(instantiated->proposition));
        return std::nullopt;
    }

    std::optional<kernel::Proposition> motive = rewrite_context(*inner, equality->lhs);
    if (!motive.has_value()) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + rewritten.evidence.name + "' rewrites " + kernel::describe(equality->lhs) +
                   ", which does not occur in the goal",
               "the goal here is " + kernel::describe(*inner));
        return std::nullopt;
    }

    const kernel::Proposition remaining = kernel::instantiate(*motive, equality->rhs);
    std::optional<kernel::ProofTerm> rest;
    {
        const Underneath introduced(body, binders);
        rest = prove(body, remaining, engine);
    }
    if (!rest.has_value()) {
        return std::nullopt;
    }

    return quantify(binders, kernel::ProofTerm::equality_elimination(equality->type, equality->lhs, equality->rhs,
                                                                     std::move(*motive), std::move(instantiated->term),
                                                                     std::move(*rest)));
}

} // namespace

namespace detail::generation {

std::optional<kernel::ProofTerm> prove(Body& body, const kernel::Proposition& goal, diagnostics::Engine& engine) {
    const vir::Proof& proof = body.proof;

    if (body.cursor >= body.steps->size()) {
        report(engine, diagnostics::Category::ProofFailure, body.body_location,
               "proof '" + proof.name + "' leaves a goal open",
               "nothing in its body establishes " + kernel::describe(goal));
        return std::nullopt;
    }

    const vir::ProofStep& step = (*body.steps)[body.cursor++];

    if (const auto* product = std::get_if<vir::ProductStep>(&step.node)) {
        const auto descriptor = decomposition::decompose({product->subject, step.location});
        if (!std::holds_alternative<decomposition::ProductDecomposition>(descriptor)) {
            report(engine, diagnostics::Category::ProofFailure, step.location, "malformed product decomposition");
            return std::nullopt;
        }
        Body nested = body;
        nested.steps = &product->steps;
        nested.cursor = 0;
        nested.body_location = step.location;
        auto result = prove(nested, goal, engine);
        if (result && nested.cursor != product->steps.size()) {
            report(engine, diagnostics::Category::ProofFailure, step.location, "unused product proof statements");
            return std::nullopt;
        }
        return result;
    }

    if (const auto* cases = std::get_if<vir::CasesStep>(&step.node))
        return prove_cases(body, step, *cases, goal, engine);

    if (const auto* induction = std::get_if<vir::InductionStep>(&step.node))
        return prove_induction(body, step, *induction, goal, engine);

    if (std::holds_alternative<vir::ReflexivityStep>(step.node)) {
        return definitional_evidence(goal);
    }

    if (const auto* assumed = std::get_if<vir::AssumeStep>(&step.node)) {
        return suppose(body, step, *assumed, goal, engine);
    }

    if (const auto* rewritten = std::get_if<vir::RewriteStep>(&step.node)) {
        return transport(body, step, *rewritten, goal, engine);
    }

    if (const auto* contradiction = std::get_if<vir::ContradictionStep>(&step.node)) {
        return prove_contradiction(body, step, *contradiction, goal, engine);
    }

    const auto* exact = std::get_if<vir::ExactStep>(&step.node);
    const vir::Reference& reference = exact != nullptr ? exact->evidence : std::get<vir::ApplyStep>(step.node).evidence;
    const std::vector<vir::Expr>& arguments =
        exact != nullptr ? exact->arguments : std::get<vir::ApplyStep>(step.node).arguments;

    std::vector<kernel::Type> binders;
    const kernel::Proposition* inner = under_quantifiers(goal, binders);
    const std::size_t depth = body.depth + binders.size();

    std::optional<Instantiation> evidence = named_evidence(body, step, reference, engine);
    if (!evidence.has_value()) {
        return std::nullopt;
    }

    std::optional<Instantiation> instantiated =
        instantiate_evidence(proof, reference.name, std::move(*evidence), arguments, depth, body.definitions, engine);
    if (!instantiated.has_value()) {
        return std::nullopt;
    }

    // Two readings of the statement, in a fixed order: the goal as it stands,
    // then the goal with its own quantifiers introduced. An argument may be a
    // closed term, or it may mention a variable, in which case the statement it
    // leaves means something only underneath the binders it was stated in and
    // the second reading is the only one available. Which reading the goal asks
    // for is settled by comparing propositions, never by searching.
    std::string reason;
    std::optional<std::size_t> discharge;
    if (!instantiated->open || binders.empty()) {
        discharge = premises_before_the_goal(body.context, instantiated->proposition, goal, exact != nullptr, reason);
    }
    if (discharge.has_value()) {
        binders.clear();
    } else if (!binders.empty()) {
        std::string deeper;
        discharge = premises_before_the_goal(body.context, instantiated->proposition, *inner, exact != nullptr, deeper);
    }

    if (!discharge.has_value()) {
        std::string note = "it establishes " + kernel::describe(instantiated->proposition);
        // A binder a proposition writes for itself has no name a statement can
        // use, so evidence left quantified over one cannot be instantiated here.
        if (std::holds_alternative<kernel::Forall>(instantiated->proposition.node) &&
            !std::holds_alternative<kernel::Forall>(inner->node)) {
            note += ", which stays quantified over a variable no statement here can name";
        }
        note += ", and the goal is " + kernel::describe(goal);
        report(engine, diagnostics::Category::ProofFailure, step.location,
               exact != nullptr ? "'" + reference.name + "' does not prove what proof '" + proof.name + "' claims"
                                : "the conclusion of '" + reference.name + "' cannot be applied to what proof '" +
                                      proof.name + "' claims: " + reason,
               std::move(note));
        return std::nullopt;
    }

    kernel::ProofTerm term = std::move(instantiated->term);
    kernel::Proposition current = std::move(instantiated->proposition);
    {
        // A premise left by the reading that introduced the goal's quantifiers is
        // stated underneath them.
        const Underneath introduced(body, binders);
        for (std::size_t remaining = *discharge; remaining > 0; --remaining) {
            const auto& implication = std::get<kernel::Implies>(current.node);
            kernel::Proposition premise = *implication.premise;
            kernel::Proposition conclusion = *implication.conclusion;

            // The premise this application leaves is a goal like any other, and
            // the statements that follow are what close it.
            std::optional<kernel::ProofTerm> discharged = prove(body, premise, engine);
            if (!discharged.has_value()) {
                return std::nullopt;
            }
            term =
                kernel::ProofTerm::implication_elimination(std::move(current), std::move(term), std::move(*discharged));
            current = std::move(conclusion);
        }
    }

    const auto& target = binders.empty() ? goal : *inner;
    if (convertible_equality(body.context, current, target)) {
        term = convert_equality(current, target, std::move(term));
    }
    return quantify(binders, std::move(term));
}

} // namespace detail::generation

} // namespace cppl::obligations
