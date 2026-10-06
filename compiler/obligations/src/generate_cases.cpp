// Proving by contradiction, by cases and by induction, and discharging the
// claims that a path cannot occur.

#include "contradiction.hpp"
#include "cppl/decomposition/decomposition.hpp"
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
#include "cppl/vir/ids.hpp"
#include "cppl/vir/module.hpp"
#include "generate_detail.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations {

using detail::generation::Body;
using detail::generation::report;

namespace {

using detail::arithmetic_fact;

using detail::Standing;
using detail::Unestablished;

} // namespace

namespace detail::generation {

// Records an established omission as the obligation CASE-012 and CASE-016
// require, with an origin, provenance and identity of its own and a goal that
// stands apart from the proof it was written in: that the premises standing in
// the omitted case, closed over the binders they stand under, cannot all hold:
// that together they establish `False`. Its evidence is the refutation `absurd`,
// which the kernel checks against that goal on its own.
//
// Every premise in `body` is stated at the body's depth (`Underneath`), so they
// are introduced after all the binders, in the order they were assumed; that
// keeps each hypothesis the refutation names at the same position it had where
// the refutation was built.
void record_omission(const Body& body, const vir::CaseArm& arm, const vir::Reference& named,
                     const kernel::ProofTerm& absurd) {
    kernel::Proposition goal = kernel::Proposition::falsity();
    kernel::ProofTerm evidence = absurd;
    for (std::size_t index = body.assumptions.size(); index > 0; --index) {
        const kernel::Proposition& premise = body.assumptions[index - 1].second;
        goal = kernel::Proposition::implication(premise, std::move(goal));
        evidence = kernel::ProofTerm::implication_introduction(premise, std::move(evidence));
    }
    for (const kernel::Type& binder : std::views::reverse(body.binders)) {
        goal = kernel::Proposition::for_all(binder, std::move(goal));
        evidence = kernel::ProofTerm::forall_introduction(binder, std::move(evidence));
    }

    // The refutation may name a trusted law, whose hypothesis stands outside
    // everything closed over above. The claim is therefore made relative to
    // every trusted law of the proof it is written in: more than the refutation
    // may need, never less.
    Obligation obligation;
    if (body.trusted != nullptr) {
        obligation.assumptions = *body.trusted;
        for (TrustedPremise& premise : std::views::reverse(obligation.assumptions)) {
            premise.direct = false;
            evidence = kernel::ProofTerm::implication_introduction(premise.proposition, std::move(evidence));
        }
    }
    obligation.origin = Origin::OmittedCase;
    obligation.subject = "case '" + arm.label + "' of proof '" + body.proof.name + "'";
    if (const auto* proof = std::get_if<vir::ProofRef>(&named.node)) {
        if (const auto built = body.built_index.find(proof->proof.value); built != body.built_index.end()) {
            const WrittenProof& used = body.built[built->second];
            obligation.uses.push_back(ProofUse{used.name, used.range.begin});
        }
    }
    obligation.goal = std::move(goal);
    obligation.evidence = std::move(evidence);
    obligation.range = source::SourceRange{arm.location, {}};

    obligation.id = identify_impossibility(Origin::OmittedCase, body.context, obligation.subject, obligation.goal,
                                           body.omissions->size());
    body.omissions->push_back(std::move(obligation));
}

} // namespace detail::generation

namespace {

// At most this many arms in one statement, so malformed VIR cannot make
// evidence construction grow without bound. Proof-resource limits apply too.
constexpr std::size_t kMaxCaseArms = 64;

} // namespace

namespace detail::generation {

// Case splitting is derived evidence, not a new rule.
//
// A provider describes a partition as a list of discriminator conditions. Each
// is an ordinary modeled comparison, and machine comparison is total, so
// conditional elimination on one splits any goal into two branches the kernel
// checks independently. Splitting on the discriminators in turn leaves one
// remaining branch, in which every discriminator is known false; that is the
// tail case, and it receives their conjunction. No completeness claim about the
// representation reaches the kernel: exhaustiveness is a property of evidence
// the kernel rechecks, and a provider that described the wrong partition can
// only fail to produce a proof, never forge one.
std::optional<kernel::ProofTerm> prove_cases(Body& body, const vir::ProofStep& step, const vir::CasesStep& cases,
                                             const kernel::Proposition& goal, diagnostics::Engine& engine) {
    std::vector<kernel::Type> binders;
    const auto* inner = under_quantifiers(goal, binders);
    Body scoped = body;
    scoped.depth += binders.size();
    scoped.binders.insert(scoped.binders.end(), binders.begin(), binders.end());
    for (auto& assumption : scoped.assumptions)
        assumption.second = kernel::shift(assumption.second, static_cast<std::uint32_t>(binders.size()));

    // The partition is asked of the provider here, not taken from the arms.
    // What the frontend recorded is only which case each arm claims; whether
    // those claims cover the representation is decided against the provider's
    // own description.
    const decomposition::Decomposition decomposed = decomposition::decompose({cases.subject, step.location});
    const auto* sum = std::get_if<decomposition::SumDecomposition>(&decomposed);
    if (sum == nullptr) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "the subject of this case split has no sum decomposition");
        return std::nullopt;
    }

    TermLowering lowering(body.definitions, scoped.depth);
    std::vector<kernel::Term> discriminators;
    for (const decomposition::CaseDescriptor& descriptor : sum->cases) {
        auto condition = lowering.lower(descriptor.discriminator);
        if (!condition) {
            report(engine, diagnostics::Category::ProofFailure, step.location,
                   "case '" + descriptor.label.text + "' cannot be stated: " + condition.error().reason);
            return std::nullopt;
        }
        discriminators.push_back(std::move(*condition));
    }

    // Which arm proves which case, checked against the provider's partition.
    const bool residual_required = sum->exhaustiveness == decomposition::ExhaustivenessModel::ResidualRequired;
    std::vector<const vir::CaseArm*> claimed(sum->cases.size(), nullptr);
    const vir::CaseArm* residual = nullptr;
    for (const vir::CaseArm& arm : cases.arms) {
        if (arm.descriptor.has_value()) {
            if (*arm.descriptor >= claimed.size() || claimed[*arm.descriptor] != nullptr) {
                report(engine, diagnostics::Category::ProofFailure, arm.location, "malformed case evidence");
                return std::nullopt;
            }
            claimed[*arm.descriptor] = &arm;
            continue;
        }
        if (residual != nullptr || !residual_required) {
            report(engine, diagnostics::Category::ProofFailure, arm.location, "malformed case evidence");
            return std::nullopt;
        }
        residual = &arm;
    }
    // A partition with no residual case and no cases at all would claim the
    // subject has no states, which is not something evidence can establish.
    if (std::ranges::find(claimed, nullptr) != claimed.end() || (residual_required && residual == nullptr) ||
        (!residual_required && sum->cases.empty()) || cases.arms.size() > kMaxCaseArms) {
        report(engine, diagnostics::Category::ProofFailure, step.location, "incomplete case evidence");
        return std::nullopt;
    }

    // The branch in which every discriminator is false. When the partition
    // needs a residual case that is its arm; otherwise the last named case is
    // exactly the negation of the others, and splitting stops one short of it.
    const std::size_t splits = residual_required ? sum->cases.size() : sum->cases.size() - 1;
    const vir::CaseArm& tail = residual_required ? *residual : *claimed.back();

    constexpr auto anonymous = std::numeric_limits<std::uint32_t>::max();
    const auto arm_proof = [&](Body context, const vir::CaseArm& arm) -> std::optional<kernel::ProofTerm> {
        context.steps = &arm.steps;
        context.cursor = 0;
        context.body_location = arm.location;
        // An omitted case is discharged by one contradiction and nothing else
        // (CASE-004 clause 2). The case's discriminator already stands in this
        // context, so the evidence is checked under it exactly as an arm's body
        // would be (CASE-013), and the omission is recorded as an obligation of
        // its own (CASE-016).
        if (arm.omitted) {
            const auto* discharge =
                arm.steps.size() == 1 ? std::get_if<vir::ContradictionStep>(&arm.steps[0].node) : nullptr;
            if (discharge == nullptr) {
                report(engine, diagnostics::Category::ProofFailure, arm.location, "malformed omitted case");
                return std::nullopt;
            }
            return prove_contradiction(context, arm.steps[0], *discharge, *inner, engine, &arm);
        }
        auto evidence = prove(context, *inner, engine);
        if (evidence && context.cursor != arm.steps.size()) {
            report(engine, diagnostics::Category::ProofFailure, arm.steps[context.cursor].location,
                   "case arm has already closed its goal");
            return std::nullopt;
        }
        return evidence;
    };

    const auto split = [&](auto&& self, Body context, std::size_t position) -> std::optional<kernel::ProofTerm> {
        if (position == splits) {
            if (position == 0) {
                return arm_proof(context, tail);
            }
            // Supply the tail case's fact: the left-associated conjunction of
            // every exclusion, in the partition's own order, built from the
            // path facts this branch already carries.
            auto fact = context.assumptions[context.assumptions.size() - position].second;
            auto evidence = kernel::ProofTerm::hypothesis({static_cast<std::uint32_t>(position - 1)});
            for (std::size_t index = 1; index < position; ++index) {
                fact = kernel::Proposition::conjunction(
                    std::move(fact), context.assumptions[context.assumptions.size() - position + index].second);
                evidence = kernel::ProofTerm::conjunction_introduction(
                    std::move(evidence),
                    kernel::ProofTerm::hypothesis({static_cast<std::uint32_t>(position - index - 1)}));
            }
            context.assumptions.emplace_back(anonymous, fact);
            auto arm = arm_proof(context, tail);
            if (!arm)
                return std::nullopt;
            return kernel::ProofTerm::implication_elimination(
                kernel::Proposition::implication(fact, *inner),
                kernel::ProofTerm::implication_introduction(fact, std::move(*arm)), std::move(evidence));
        }

        const kernel::Term& condition = discriminators[position];
        const auto positive = kernel::predicate(condition, true);
        const auto negative = kernel::predicate(condition, false);
        Body holds = context;
        holds.assumptions.emplace_back(anonymous, positive);
        auto at_case = arm_proof(holds, *claimed[position]);
        if (!at_case)
            return std::nullopt;
        context.assumptions.emplace_back(anonymous, negative);
        auto otherwise = self(self, context, position + 1);
        if (!otherwise)
            return std::nullopt;
        return kernel::ProofTerm::conditional_elimination(
            kernel::Type{kernel::kBoolean}, condition, kernel::Term::literal(kernel::kBoolean, 1),
            kernel::Term::literal(kernel::kBoolean, 1), kernel::shift(*inner, 1),
            kernel::ProofTerm::implication_introduction(positive, std::move(*at_case)),
            kernel::ProofTerm::implication_introduction(negative, std::move(*otherwise)));
    };

    auto evidence = split(split, scoped, 0);
    if (!evidence)
        return std::nullopt;
    return quantify(binders, std::move(*evidence));
}

} // namespace detail::generation

namespace {

// One case of `induction x;`, closed by automation (SPEC.md INDUCT-005).
//
// The case is stated as a claim of its own, closed over every binder and every
// premise standing where the statement is written, so automation sees exactly
// what a written arm would and nothing more: no trusted law is among the
// premises, because no strategy ever supposes one. The candidate is checked by
// the kernel against that claim here, and is then used by eliminating it at
// the binders and premises it was closed over, so what the whole proof rests on
// is checked again with it.
std::optional<kernel::ProofTerm> automatic_case(const Body& body, const kernel::Proposition& goal,
                                                const std::string& label, const vir::InductionStep& induction,
                                                const source::SourceLocation& location, diagnostics::Engine& engine) {
    kernel::Proposition closed = goal;
    for (std::size_t index = body.assumptions.size(); index > 0; --index) {
        closed = kernel::Proposition::implication(body.assumptions[index - 1].second, std::move(closed));
    }
    for (const kernel::Type& binder : std::views::reverse(body.binders)) {
        closed = kernel::Proposition::for_all(binder, std::move(closed));
    }

    std::optional<kernel::ProofTerm> candidate;
    if (body.automation != nullptr && *body.automation) {
        candidate = (*body.automation)(body.context, closed);
    }
    if (!candidate.has_value() || !kernel::check(body.context, closed, *candidate, kernel::CoreLimits{})) {
        report(engine, diagnostics::Category::ProofFailure, location,
               "automation does not establish the '" + label + "' case of induction over '" + induction.subject + "'",
               "the case to prove is " + kernel::describe(goal) + "; write the arms to prove it explicitly");
        return std::nullopt;
    }

    kernel::ProofTerm term = std::move(*candidate);
    kernel::Proposition current = std::move(closed);
    for (std::size_t position = 0; position < body.binders.size(); ++position) {
        const kernel::Term argument = kernel::Term::variable(kernel::parameter_reference(body.depth, position));
        kernel::Proposition instance = kernel::instantiate(*std::get<kernel::Forall>(current.node).body, argument);
        term = kernel::ProofTerm::forall_elimination(std::move(current), std::move(term), argument);
        current = std::move(instance);
    }
    for (std::size_t position = 0; position < body.assumptions.size(); ++position) {
        kernel::Proposition conclusion = *std::get<kernel::Implies>(current.node).conclusion;
        term = kernel::ProofTerm::implication_elimination(
            std::move(current), std::move(term),
            kernel::ProofTerm::hypothesis(
                kernel::HypothesisIndex{static_cast<std::uint32_t>(body.assumptions.size() - 1 - position)}));
        current = std::move(conclusion);
    }
    return term;
}

} // namespace

namespace detail::generation {

// `induction x { zero => ... successor(pred) => ... }` and `induction x;`
// (SPEC.md 21, GRAMMAR.md 5.8).
//
// The subject names the quantifier `induction.level` binders into the goal. The
// binders in front of it are introduced as a universal introduction would, and
// the quantifier itself is established by the kernel's unsigned induction rule
// (FOUNDATIONS.md 74). Its two premises are stated here by the kernel's own
// functions, which the kernel calls again when it checks the evidence: the
// zero arm proves the goal at 0, and the successor arm proves it at the
// predecessor plus one with the range premise and the induction hypothesis
// standing as premises `assume` can name (INDUCT-001, INDUCT-003). Those two
// premises are the step's own implication introductions, so neither exists
// outside the successor arm (FOUNDATIONS.md 10).
//
// The quantifier must still stand in the goal: a statement that has already
// introduced it, such as an earlier `assume`, leaves a variable, and induction
// over a variable would need the premises mentioning it generalized first,
// which this implementation does not do. That is refused rather than read as
// induction over something else.
std::optional<kernel::ProofTerm> prove_induction(Body& body, const vir::ProofStep& step,
                                                 const vir::InductionStep& induction, const kernel::Proposition& goal,
                                                 diagnostics::Engine& engine) {
    if (body.depth > induction.level) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "induction over '" + induction.subject + "' comes after a statement that already introduced it",
               "write 'induction' before any 'assume', 'rewrite', 'cases' or 'apply' that introduces the "
               "quantifiers the goal leads with");
        return std::nullopt;
    }

    std::vector<kernel::Type> binders;
    const kernel::Proposition* at = &goal;
    while (body.depth + binders.size() < induction.level) {
        const auto* leading = std::get_if<kernel::Forall>(&at->node);
        if (leading == nullptr) {
            break;
        }
        binders.push_back(leading->binder);
        at = &*leading->body;
    }
    const auto* quantified = std::get_if<kernel::Forall>(&at->node);
    const std::optional<kernel::Type> subject = lower_type(induction.type);
    if (body.depth + binders.size() != induction.level || quantified == nullptr || !subject.has_value() ||
        !(*subject == quantified->binder)) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "the goal here does not quantify over '" + induction.subject + "'",
               "the goal is " + kernel::describe(goal));
        return std::nullopt;
    }
    if (!subject->is_integer() || subject->integer_type().signedness != kernel::Signedness::Unsigned) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "induction over '" + induction.subject + "' has no principle for " + kernel::describe(*subject));
        return std::nullopt;
    }
    const kernel::IntType& type = subject->integer_type();
    const kernel::Proposition base = kernel::induction_base(type, *quantified->body);
    const kernel::Proposition successor = kernel::induction_step(type, *quantified->body);

    Body scoped = body;
    scoped.depth += binders.size();
    scoped.binders.insert(scoped.binders.end(), binders.begin(), binders.end());
    for (auto& assumption : scoped.assumptions)
        assumption.second = kernel::shift(assumption.second, static_cast<std::uint32_t>(binders.size()));

    std::optional<kernel::ProofTerm> zero;
    std::optional<kernel::ProofTerm> next;
    if (induction.automatic) {
        zero = automatic_case(scoped, base, "zero", induction, step.location, engine);
        if (!zero)
            return std::nullopt;
        next = automatic_case(scoped, successor, "successor", induction, step.location, engine);
        if (!next)
            return std::nullopt;
    } else {
        const auto arm = [&](Body context, const std::vector<vir::ProofStep>& steps,
                             const source::SourceLocation& location,
                             const kernel::Proposition& case_goal) -> std::optional<kernel::ProofTerm> {
            context.steps = &steps;
            context.cursor = 0;
            context.body_location = location;
            auto evidence = prove(context, case_goal, engine);
            if (evidence && context.cursor != steps.size()) {
                report(engine, diagnostics::Category::ProofFailure, steps[context.cursor].location,
                       "induction arm has already closed its goal");
                return std::nullopt;
            }
            return evidence;
        };
        zero = arm(scoped, induction.zero, induction.zero_location, base);
        if (!zero)
            return std::nullopt;

        // forall n. n < max -> P(n) -> P(n + 1): the arm stands under `n`, with
        // the range premise and the hypothesis supposed in that order.
        const auto& stepped = std::get<kernel::Forall>(successor.node);
        const auto& range = std::get<kernel::Implies>(stepped.body->node);
        const auto& hypothesis = std::get<kernel::Implies>(range.conclusion->node);
        constexpr auto anonymous = std::numeric_limits<std::uint32_t>::max();
        Body under = scoped;
        under.depth += 1;
        under.binders.push_back(stepped.binder);
        for (auto& assumption : under.assumptions)
            assumption.second = kernel::shift(assumption.second, 1);
        under.assumptions.emplace_back(anonymous, *range.premise);
        under.assumptions.emplace_back(anonymous, *hypothesis.premise);
        auto closed = arm(under, induction.successor, induction.successor_location, *hypothesis.conclusion);
        if (!closed)
            return std::nullopt;
        next = kernel::ProofTerm::forall_introduction(
            stepped.binder,
            kernel::ProofTerm::implication_introduction(
                *range.premise, kernel::ProofTerm::implication_introduction(*hypothesis.premise, std::move(*closed))));
    }

    return quantify(binders,
                    kernel::ProofTerm::unsigned_induction(quantified->binder, std::move(*zero), std::move(*next)));
}

// Builds the evidence each claim that a runtime path cannot occur names
// (SPEC.md VERIFIED-023, CASE-013).
//
// A claim's goal is its path's fresh values and supposed facts closed over
// `False`. Its evidence introduces them in turn and, beneath all of them,
// refutes the named proof's conclusion together with every fact into `False`:
// the mechanism an omitted case and a `contradiction` statement use, applied to
// the facts of a path rather than to the premises of a proof. A claim that
// cannot be given evidence is reported once, here, and stands unproven; nothing
// looks for other evidence for it (CASE-005, CASE-015).
void discharge_path_claims(Program& program, diagnostics::Engine& engine) {
    for (PathClaim& claim : program.path_claims) {
        Obligation& obligation = program.obligations[claim.obligation];
        const auto refuse = [&](std::string message, std::string note) {
            obligation.refusal = message;
            report(engine, diagnostics::Category::ProofFailure, claim.location, std::move(message), std::move(note));
        };

        // A name no proof declares was reported where it was resolved.
        if (!claim.proof.has_value()) {
            obligation.refusal = "'" + claim.evidence + "' names no proof declaration";
            continue;
        }
        // An omission in a split's arm uses this mechanism for a claim of its
        // own, and is refused in its own words (SPEC.md CASE-012, CASE-016).
        const bool omission = obligation.origin == Origin::OmittedCase;
        const auto proof = std::ranges::find(program.proofs, *claim.proof, &WrittenProof::id);
        if (proof == program.proofs.end()) {
            refuse("proof '" + claim.evidence + "' was not admitted, so the claim that " +
                       (omission ? obligation.subject : "runtime path '" + obligation.subject + "'") +
                       " cannot occur has no evidence",
                   "the reason it was not admitted is reported above");
            continue;
        }
        obligation.uses.push_back(ProofUse{proof->name, proof->range.begin});

        // A proof established relative to trusted laws is used relative to the
        // same ones (SPEC.md TRUSTED-006). They are supposed outside everything
        // the claim's goal introduces, so each stands past every fact of the
        // path, and each premise of the proof is discharged by its supposition.
        std::uint32_t facts = 0;
        for (const kernel::Proposition* walk = &obligation.goal;;) {
            if (const auto* quantified = std::get_if<kernel::Forall>(&walk->node)) {
                walk = &*quantified->body;
            } else if (const auto* implication = std::get_if<kernel::Implies>(&walk->node)) {
                ++facts;
                walk = &*implication->conclusion;
            } else {
                break;
            }
        }
        const std::vector<TrustedPremise>& trusted = proof->assumptions;
        kernel::ProofTerm term = proof->term;
        kernel::Proposition named = relative_to(trusted, proof->goal);
        for (std::size_t position = 0; position < trusted.size(); ++position) {
            kernel::Proposition conclusion = *std::get<kernel::Implies>(named.node).conclusion;
            term = kernel::ProofTerm::implication_elimination(
                std::move(named), std::move(term),
                kernel::ProofTerm::hypothesis(
                    kernel::HypothesisIndex{facts + static_cast<std::uint32_t>(trusted.size() - 1 - position)}));
            named = std::move(conclusion);
        }
        bool instantiated = true;
        for (std::size_t index = 0; index < claim.arguments.size() && instantiated; ++index) {
            const auto* quantified = std::get_if<kernel::Forall>(&named.node);
            if (quantified == nullptr) {
                refuse("proof '" + claim.evidence + "' is instantiated at more arguments than it quantifies over",
                       "at this argument it establishes " + kernel::describe(named) +
                           ", which quantifies over nothing");
                instantiated = false;
            } else if (!(quantified->binder == claim.argument_types[index])) {
                refuse("proof '" + claim.evidence + "' quantifies over '" + kernel::describe(quantified->binder) +
                           "' and cannot be instantiated at a term of type '" +
                           kernel::describe(claim.argument_types[index]) + "'",
                       "argument " + std::to_string(index + 1) + " of '" + claim.evidence + "'");
                instantiated = false;
            } else {
                kernel::Proposition eliminated = kernel::instantiate(*quantified->body, claim.arguments[index]);
                term = kernel::ProofTerm::forall_elimination(std::move(named), std::move(term), claim.arguments[index]);
                named = std::move(eliminated);
            }
        }
        if (!instantiated) {
            continue;
        }
        if (!std::holds_alternative<kernel::Eq>(named.node)) {
            refuse("'" + claim.evidence + "' does not establish an equality, so it cannot state a contradiction",
                   "it establishes " + kernel::describe(named));
            continue;
        }
        if (!arithmetic_fact(program.context, named)) {
            refuse("'" + claim.evidence +
                       "' establishes an equality linear arithmetic cannot state, so it cannot state a contradiction",
                   "it establishes " + kernel::describe(named));
            continue;
        }

        // The goal's introductions, outermost first, and the premises they put
        // in scope, each with the number of binders standing where it is.
        struct Introduction {
            const kernel::Proposition* premise = nullptr; // null for a binder
            const kernel::Type* binder = nullptr;
            std::size_t binders = 0;
        };
        std::vector<Introduction> introductions;
        std::size_t binders = 0;
        const kernel::Proposition* current = &obligation.goal;
        while (true) {
            if (const auto* quantified = std::get_if<kernel::Forall>(&current->node)) {
                introductions.push_back(Introduction{nullptr, &quantified->binder, binders++});
                current = &*quantified->body;
            } else if (const auto* implication = std::get_if<kernel::Implies>(&current->node)) {
                introductions.push_back(Introduction{&*implication->premise, nullptr, binders});
                current = &*implication->conclusion;
            } else {
                break;
            }
        }

        // Every fact of the path, innermost first, restated beneath all the
        // binders, where the refutation stands.
        std::vector<Standing> standing;
        std::uint32_t hypothesis = 0;
        for (const Introduction& introduced : std::views::reverse(introductions)) {
            if (introduced.premise == nullptr) {
                continue;
            }
            standing.push_back(
                Standing{kernel::shift(*introduced.premise, static_cast<std::uint32_t>(binders - introduced.binders)),
                         kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{hypothesis++})});
        }

        std::expected<kernel::ProofTerm, Unestablished> absurd = detail::refute(
            program.context, kernel::ArithmeticFact{named, kernel::Box<kernel::ProofTerm>{std::move(term)}}, standing);
        if (!absurd.has_value()) {
            refuse(omission ? "omitted " + obligation.subject + " is not shown to be impossible"
                            : "runtime path '" + obligation.subject + "' is not shown to be unreachable",
                   absurd.error().kind == Unestablished::Kind::Unreadable
                       ? "the facts established on that path cannot be stated as linear arithmetic: " +
                             absurd.error().detail
                       : "'" + claim.evidence + "' establishes " + kernel::describe(named) +
                             (omission ? ", and no contradiction with the facts established on that path, including "
                                         "the case's own discriminator, was found"
                                       : ", and no contradiction with the facts established on that path was found"));
            continue;
        }

        kernel::ProofTerm evidence = std::move(*absurd);
        for (const Introduction& introduced : std::views::reverse(introductions)) {
            evidence = introduced.premise != nullptr
                           ? kernel::ProofTerm::implication_introduction(*introduced.premise, std::move(evidence))
                           : kernel::ProofTerm::forall_introduction(*introduced.binder, std::move(evidence));
        }
        obligation.assumptions = trusted;
        for (TrustedPremise& premise : std::views::reverse(obligation.assumptions)) {
            premise.direct = false;
            evidence = kernel::ProofTerm::implication_introduction(premise.proposition, std::move(evidence));
        }
        obligation.evidence = std::move(evidence);
    }
    program.path_claims.clear();
}

} // namespace detail::generation

} // namespace cppl::obligations
