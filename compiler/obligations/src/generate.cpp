#include "cppl/obligations/generate.hpp"

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/interface.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/capability.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"
#include "definedness.hpp"
#include "generate_detail.hpp"
#include "lowering.hpp"
#include "walks.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations {

using detail::generation::Body;
using detail::generation::claimed_proposition;
using detail::generation::discharge_path_claims;
using detail::generation::identify;
using detail::generation::lower_proposition;
using detail::generation::lower_type;
using detail::generation::make_rewrite_context;
using detail::generation::prove;
using detail::generation::quantify_over;
using detail::generation::report;
using detail::generation::TermLowering;

namespace {

using detail::collect_callees;
using detail::Failure;

using detail::DefinitionMap;

// Lowers each written proof into a kernel proof term.
//
// A step is lowered only once the proof it names has been lowered, so the
// dependency graph is traversed in order and anything left over is circular.
// No step is admitted on the strength of what it is called: `exact` must offer
// the claimed proposition itself, and `apply` must offer a conclusion that
// proposition can accept. Both then go to the kernel like any other evidence.
void lower_proofs(const vir::Module& module, const elaboration::Result& elaborated, const DefinitionMap& definitions,
                  Program& program, diagnostics::Engine& engine, const CaseAutomation& automation) {
    program.refused_proofs = elaborated.laws_with_refused_proofs;

    std::map<std::uint32_t, const Obligation*> goals;
    std::map<std::uint32_t, const Obligation*> direct_goals;
    for (const Obligation& obligation : program.obligations) {
        if (obligation.law.has_value()) {
            goals.emplace(obligation.law->value, &obligation);
        }
        if (obligation.proof)
            direct_goals.emplace(obligation.proof->value, &obligation);
    }

    std::set<std::uint32_t> declared;
    std::vector<const vir::Proof*> pending;
    for (const vir::Proof& proof : module.proofs) {
        declared.insert(proof.id.value);
        if (proof.law ? goals.contains(proof.law->value) : direct_goals.contains(proof.id.value)) {
            pending.push_back(&proof);
        }
    }

    std::map<std::uint32_t, std::size_t> lowered; // proof id -> index in program.proofs
    std::set<std::uint32_t> refused;
    // The omitted cases admitted proofs established. They join the program's
    // obligations only after the loop, which holds pointers into that list.
    std::vector<Obligation> established_omissions;

    bool progress = true;
    while (progress) {
        progress = false;
        for (auto candidate = pending.begin(); candidate != pending.end();) {
            const vir::Proof& proof = **candidate;
            const Obligation& obligation = proof.law ? *goals.at(proof.law->value) : *direct_goals.at(proof.id.value);

            const auto refuse = [&] {
                refused.insert(proof.id.value);
                if (proof.law)
                    program.refused_proofs.push_back(*proof.law);
                candidate = pending.erase(candidate);
                progress = true;
            };

            // Evidence is built in dependency order: a statement's proof must
            // already have a term before this body can be lowered at all, which
            // is what keeps circular evidence from ever producing one.
            bool admitted = true;
            bool ready = true;
            // The trusted laws this proof rests on: those it names, and those
            // every proof it uses rests on. Keyed by law, so they are supposed
            // in declaration order whatever order they are met in.
            std::map<vir::LawId, TrustedPremise> premises;
            std::vector<ProofUse> uses;
            const auto rests_on = [&premises](const TrustedPremise& premise, bool direct) {
                auto [entry, added] = premises.emplace(premise.law, premise);
                entry->second.direct = added ? direct : entry->second.direct || direct;
            };
            std::vector<const vir::ProofStep*> dependencies;
            const auto collect = [&](auto&& self, const std::vector<vir::ProofStep>& steps) -> void {
                for (const auto& step : steps) {
                    dependencies.push_back(&step);
                    if (const auto* product = std::get_if<vir::ProductStep>(&step.node))
                        self(self, product->steps);
                    if (const auto* cases = std::get_if<vir::CasesStep>(&step.node))
                        for (const auto& arm : cases->arms)
                            self(self, arm.steps);
                    if (const auto* induction = std::get_if<vir::InductionStep>(&step.node)) {
                        self(self, induction->zero);
                        self(self, induction->successor);
                    }
                }
            };
            collect(collect, proof.steps);
            for (const vir::ProofStep* dependency : dependencies) {
                const vir::ProofStep& step = *dependency;
                const vir::Reference* reference = nullptr;
                if (const auto* used = std::get_if<vir::ExactStep>(&step.node)) {
                    reference = &used->evidence;
                } else if (const auto* applied = std::get_if<vir::ApplyStep>(&step.node)) {
                    reference = &applied->evidence;
                } else if (const auto* rewritten = std::get_if<vir::RewriteStep>(&step.node)) {
                    reference = &rewritten->evidence;
                } else if (const auto* refuted = std::get_if<vir::ContradictionStep>(&step.node)) {
                    reference = &refuted->evidence;
                }
                if (reference == nullptr) {
                    continue;
                }
                if (const auto* trusted = std::get_if<vir::TrustedLawRef>(&reference->node)) {
                    // Only a law that is itself an explicit assumption may be
                    // one, and it is supposed as exactly the proposition it
                    // states (TRUST.md TCB-TRUST-009). Anything else named here
                    // would be reported as trust it is not, so it is refused.
                    const auto law = goals.find(trusted->law.value);
                    if (law == goals.end() || !law->second->trusted) {
                        report(engine, diagnostics::Category::ProofFailure, step.location,
                               "trusted law '" + reference->name + "' has no stated proposition, so proof '" +
                                   proof.name + "' has no evidence",
                               "the reason the law was not stated is reported above");
                        admitted = false;
                        break;
                    }
                    rests_on(TrustedPremise{trusted->law, law->second->subject, law->second->id,
                                            law->second->range.begin, law->second->goal},
                             true);
                    continue;
                }
                const auto* named = std::get_if<vir::ProofRef>(&reference->node);
                if (named == nullptr) {
                    continue; // a premise, which needs nothing built
                }
                if (!declared.contains(named->proof.value) || refused.contains(named->proof.value)) {
                    report(engine, diagnostics::Category::ProofFailure, step.location,
                           "proof '" + reference->name + "' was not admitted, so proof '" + proof.name +
                               "' has no evidence",
                           "the reason it was not admitted is reported above");
                    admitted = false;
                    break;
                }
                if (!lowered.contains(named->proof.value)) {
                    ready = false;
                    break;
                }
                const WrittenProof& used = program.proofs[lowered.at(named->proof.value)];
                for (const TrustedPremise& premise : used.assumptions) {
                    rests_on(premise, false);
                }
                if (std::ranges::none_of(uses, [&used](const ProofUse& known) { return known.name == used.name; })) {
                    uses.push_back(ProofUse{used.name, used.range.begin});
                }
            }

            if (!admitted) {
                refuse();
                continue;
            }
            if (!ready) {
                ++candidate; // its evidence is not built yet
                continue;
            }

            const std::optional<kernel::Proposition> claimed =
                claimed_proposition(proof, obligation, definitions, engine);
            if (!claimed.has_value()) {
                refuse();
                continue;
            }

            std::vector<TrustedPremise> assumptions;
            assumptions.reserve(premises.size());
            for (auto& [law, premise] : premises) {
                assumptions.push_back(std::move(premise));
            }

            std::vector<Obligation> omissions;
            Body body{proof, program.context, program, definitions, program.proofs, lowered, {}, 0, 0};
            body.omissions = &omissions;
            body.trusted = &assumptions;
            body.automation = &automation;
            std::optional<kernel::ProofTerm> term = prove(body, *claimed, engine);
            // The proof is closed over the trusted laws it rests on, so what the
            // kernel checks is its claim relative to exactly those.
            if (term.has_value()) {
                for (const TrustedPremise& premise : std::views::reverse(assumptions)) {
                    term = kernel::ProofTerm::implication_introduction(premise.proposition, std::move(*term));
                }
            }

            if (term.has_value() && body.cursor < proof.steps.size()) {
                report(engine, diagnostics::Category::ProofFailure, proof.steps[body.cursor].location,
                       "proof '" + proof.name + "' has already closed every goal it states",
                       "the statements before this one leave nothing to prove");
                term.reset();
            }
            if (!term.has_value()) {
                refuse();
                continue;
            }

            WrittenProof written;
            written.id = proof.id;
            written.name = proof.name;
            written.law = proof.law;
            written.goal = *claimed;
            written.closes_law = proof.law.has_value() && *claimed == obligation.goal;
            written.assumptions = std::move(assumptions);
            written.uses = std::move(uses);
            written.term = std::move(*term);
            written.range = proof.range;

            // A proof that discharges its law is submitted as that law's
            // evidence and is checked there. A proof of one instance has no
            // obligation of its own, so it is checked here: an author's claim
            // is never left standing without the kernel having seen it.
            if (!written.closes_law) {
                const kernel::CheckResult checked =
                    kernel::check(program.context, relative_to(written.assumptions, written.goal), written.term,
                                  kernel::CoreLimits{});
                if (!checked.has_value()) {
                    diagnostics::Diagnostic diagnostic;
                    diagnostic.severity = diagnostics::Severity::Error;
                    diagnostic.category = diagnostics::Category::KernelRejection;
                    diagnostic.message = "proof '" + proof.name + "' does not establish what it claims";
                    diagnostic.location = proof.range.begin;
                    diagnostic.notes.push_back(
                        diagnostics::Note{"claim: " + kernel::describe(written.goal), proof.range.begin});
                    diagnostic.notes.push_back(diagnostics::Note{
                        "the kernel did not accept the evidence: " + checked.error().detail, proof.range.begin});
                    engine.report(std::move(diagnostic));
                    refuse();
                    continue;
                }
            }

            lowered.emplace(proof.id.value, program.proofs.size());
            program.proofs.push_back(std::move(written));
            std::ranges::move(omissions, std::back_inserter(established_omissions));

            candidate = pending.erase(candidate);
            progress = true;
        }
    }

    for (const vir::Proof* proof : pending) {
        report(engine, diagnostics::Category::ProofFailure, proof->steps.front().location,
               "proof '" + proof->name + "' depends on itself through the proofs it uses",
               "circular evidence is not evidence: an induction hypothesis comes only from 'induction' and its "
               "principle, never from a proof naming itself (SPEC.md 21.4)");
        if (proof->law)
            program.refused_proofs.push_back(*proof->law);
    }

    std::map<std::uint32_t, std::string> closed_by;
    for (const WrittenProof& written : program.proofs) {
        if (!written.closes_law || !written.law.has_value()) {
            continue;
        }
        const auto [owner, first] = closed_by.emplace(written.law->value, written.name);
        if (!first) {
            report(engine, diagnostics::Category::CpplSyntax, written.range.begin,
                   "law '" + goals.at(written.law->value)->subject + "' already has a proof",
                   "'" + owner->second +
                       "' establishes it; a law is discharged by exactly one "
                       "written proof, though others may prove instances of it");
        }
    }

    // Writing a proof for a law is taking responsibility for it. If none of the
    // proofs that name a law establishes the law itself, the law stays open:
    // the compiler does not quietly close with its own strategy a goal the
    // author has already said how to establish.
    std::set<std::uint32_t> reported;
    for (const vir::Proof& proof : module.proofs) {
        if (!proof.law || !goals.contains(proof.law->value) || closed_by.contains(proof.law->value)) {
            continue;
        }
        if (program.proof_refused(*proof.law) || !reported.insert(proof.law->value).second) {
            continue;
        }
        report(engine, diagnostics::Category::ProofFailure, proof.range.begin,
               "law '" + goals.at(proof.law->value)->subject +
                   "' is named by a written proof, but nothing establishes the law itself",
               "a proof of one instance does not discharge the law; write a proof that claims "
               "it at its own parameters");
        if (proof.law)
            program.refused_proofs.push_back(*proof.law);
    }

    // Last, because `goals` and `direct_goals` point into this list.
    std::ranges::move(established_omissions, std::back_inserter(program.obligations));
}

} // namespace

std::optional<kernel::Proposition> rewrite_context(const kernel::Proposition& goal, const kernel::Term& target) {
    return make_rewrite_context(goal, target);
}

std::optional<kernel::Type> detail::core_type(const vir::Type& type) {
    return lower_type(type);
}

std::expected<kernel::Term, detail::Failure> detail::lower_value(const vir::Expr& expression,
                                                                 const DefinitionMap& definitions, std::size_t binders,
                                                                 const CallBindings* calls,
                                                                 const VersionBindings* versions,
                                                                 const OpaqueBindings* opaque) {
    TermLowering lowering(definitions, binders, calls, versions, opaque);
    return lowering.lower(expression);
}

std::expected<kernel::Proposition, detail::Failure> detail::lower_predicate(const vir::Expr& expression,
                                                                            const Program& program,
                                                                            const DefinitionMap& definitions,
                                                                            std::size_t binders) {
    return lower_proposition(expression, program, definitions, binders);
}

ObligationId detail::identify_goal(const kernel::Context& context, const std::string& subject,
                                   const kernel::Proposition& goal) {
    return identify(context, subject, goal);
}

ObligationId identify_impossibility(Origin origin, const kernel::Context& context, const std::string& subject,
                                    const kernel::Proposition& goal, std::uint64_t position) {
    source::Hasher hasher;
    hasher.update_field("impossibility-v1");
    hasher.update_field(describe(origin));
    hasher.update_field(identify(context, subject, goal).digest.to_short_hex(64));
    hasher.update_u64(position);
    return ObligationId{hasher.finish()};
}

Program generate(const vir::Module& module, const elaboration::Result& elaborated, diagnostics::Engine& engine,
                 const Imports& imports, const CaseAutomation& automation) {
    Program program;
    DefinitionMap definitions;
    std::map<std::string, Failure> deferred;

    // Definitions are admitted in dependency order. The kernel only accepts a
    // definition whose callees are already present, which is what keeps the
    // definition graph acyclic and normalization terminating.
    std::vector<const vir::Function*> pending;
    for (const vir::Function& function : module.functions) {
        if (function.contract.has_value() && !function.contract->preconditions.empty()) {
            deferred.emplace(
                function.symbol.usr,
                Failure{"call-site preconditions require a verified caller body", function.range.begin, {}});
            continue;
        }
        if (function.purity == vir::Purity::Pure && function.returned_value.has_value()) {
            pending.push_back(&function);
        }
    }

    std::uint32_t next_definition = 0;
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto candidate = pending.begin(); candidate != pending.end();) {
            const vir::Function& function = **candidate;

            std::set<std::string> callees;
            collect_callees(*function.returned_value, callees);
            const bool ready =
                std::ranges::all_of(callees, [&](const std::string& callee) { return definitions.contains(callee); });
            if (!ready) {
                ++candidate;
                continue;
            }

            // A definition is a total function the kernel unfolds wherever it
            // is called, with nothing owed at the call. An operation C++
            // defines only under a condition would then run unchecked, so a
            // body that evaluates one is not a definition (SPEC.md ARITH-011).
            if (const auto site = detail::first_definedness_site(*function.returned_value)) {
                deferred.emplace(
                    function.symbol.usr,
                    Failure{"its body evaluates an operation C++ defines only under a condition (" +
                                detail::explain(*site).front() +
                                "), and a pure function is a total definition of the formal core, which owes none",
                            site->operation->provenance.range.begin,
                            {}});
                candidate = pending.erase(candidate);
                progress = true;
                continue;
            }
            TermLowering lowering(definitions, function.parameters.size());
            std::expected<kernel::Term, Failure> body = lowering.lower(*function.returned_value);
            if (!body) {
                deferred.emplace(function.symbol.usr, body.error());
                candidate = pending.erase(candidate);
                progress = true;
                continue;
            }

            kernel::Definition definition;
            definition.id = kernel::DefId{next_definition};
            definition.name = function.qualified_name;
            const std::optional<kernel::Type> result_type = lower_type(function.result);
            if (!result_type.has_value()) {
                deferred.emplace(function.symbol.usr,
                                 Failure{"the result type has no core representation", function.range.begin, {}});
                candidate = pending.erase(candidate);
                progress = true;
                continue;
            }
            definition.result = *result_type;
            for (const vir::Parameter& parameter : function.parameters) {
                const std::optional<kernel::Type> type = lower_type(parameter.type);
                if (!type.has_value()) {
                    definition.parameters.clear();
                    break;
                }
                definition.parameters.push_back(*type);
            }

            if (definition.parameters.size() != function.parameters.size()) {
                deferred.emplace(function.symbol.usr,
                                 Failure{"a parameter type has no core representation", function.range.begin, {}});
                candidate = pending.erase(candidate);
                progress = true;
                continue;
            }

            definition.body = std::move(*body);
            if (auto admitted = program.context.define(std::move(definition)); !admitted) {
                deferred.emplace(function.symbol.usr,
                                 Failure{kernel::describe(admitted.error().kind) + ": " + admitted.error().detail,
                                         function.range.begin,
                                         {}});
            } else {
                definitions.emplace(function.symbol.usr, kernel::DefId{next_definition});
                ++next_definition;
            }

            candidate = pending.erase(candidate);
            progress = true;
        }
    }

    for (const vir::Function* function : pending) {
        deferred.emplace(function->symbol.usr, Failure{"its definition is recursive, or depends on a definition that "
                                                       "could not be admitted",
                                                       function->range.begin,
                                                       {}});
    }

    // Why a definition a law reaches for is not available to the core.
    const auto explain = [&elaborated, &deferred](const Failure& failure) {
        std::string note;
        if (failure.missing_symbol.empty()) {
            return note;
        }
        if (const elaboration::FunctionRejection* rejection =
                elaborated.rejection(vir::SymbolId{failure.missing_symbol})) {
            note = "'" + rejection->name + "' was not admitted because " + rejection->reason;
        } else if (const auto deferral = deferred.find(failure.missing_symbol); deferral != deferred.end()) {
            note = deferral->second.reason;
        } else {
            note = "it is not marked pure, so it is not a definition the formal core may unfold";
        }
        return note;
    };

    // A refinement type's predicate, stated once so every site a value enters
    // that type can ask for it (SPEC.md 17.2). It is a proposition over the
    // declaration's indices and the value, and it becomes an obligation only
    // where a value actually enters the type - a declaration asserts nothing.
    for (const auto& refinement : module.refinements) {
        RefinementPredicate stated;
        stated.name = refinement.name;
        stated.identity = refinement.identity;
        stated.statement = vir::describe(refinement.predicate);
        bool modeled = true;
        for (const auto& index : refinement.indices) {
            const std::optional<kernel::Type> type = detail::core_type(index.type);
            if (!type.has_value()) {
                report(engine, diagnostics::Category::UnsupportedSemantics, refinement.range.begin,
                       "refinement type '" + refinement.name + "' has an index of type '" + vir::describe(index.type) +
                           "', which the formal core does not represent");
                modeled = false;
                break;
            }
            stated.parameters.push_back(*type);
        }
        if (!modeled) {
            continue;
        }
        const std::optional<kernel::Type> base = detail::core_type(refinement.base);
        if (!base.has_value()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, refinement.range.begin,
                   "refinement type '" + refinement.name + "' refines '" + vir::describe(refinement.base) +
                       "', which the formal core does not represent");
            continue;
        }
        stated.parameters.push_back(*base);

        auto predicate = lower_proposition(refinement.predicate, program, definitions, stated.parameters.size());
        if (!predicate) {
            report(engine, diagnostics::Category::UnsupportedSemantics, predicate.error().location,
                   "refinement type '" + refinement.name +
                       "' cannot be stated to the formal core: " + predicate.error().reason,
                   explain(predicate.error()));
            continue;
        }
        stated.predicate = std::move(*predicate);
        // A validation runs the predicate on whatever value it is given, so an
        // operation C++ defines only under a condition on its operands would
        // make the test itself undefined for some value (SPEC.md
        // RUNTIMECHECK-020, ARITH-010).
        if (detail::first_definedness_site(refinement.predicate).has_value()) {
            stated.unvalidatable = "its predicate evaluates an operation C++ defines only under a condition on its "
                                   "operands, so testing some value would have undefined behavior";
        }
        // A validation tests the predicate this declaration states, which is
        // membership only where the base type adds none of its own.
        if (!refinement.base.refinements.empty()) {
            stated.unvalidatable = "its base type is itself a refinement type, whose predicate a validation of '" +
                                   refinement.name + "' would not test";
        }
        program.refinements.push_back(std::move(stated));
    }

    for (const vir::Law& law : module.laws) {
        std::expected<kernel::Proposition, Failure> conclusion =
            lower_proposition(law.proposition, program, definitions, law.parameters.size());
        if (!conclusion) {
            report(engine, diagnostics::Category::UnsupportedSemantics,
                   conclusion.error().location.is_valid() ? conclusion.error().location : law.proposition_range.begin,
                   "law '" + law.name + "' cannot be stated to the formal core: " + conclusion.error().reason,
                   explain(conclusion.error()));
            continue;
        }

        kernel::Proposition goal = std::move(*conclusion);

        // A precondition asserts nothing. It is what the conclusion is stated
        // under, so a law that has one states the implication between them
        // (GRAMMAR.md 3).
        if (law.premise.has_value()) {
            std::expected<kernel::Proposition, Failure> premise =
                lower_proposition(*law.premise, program, definitions, law.parameters.size());
            if (!premise) {
                report(engine, diagnostics::Category::UnsupportedSemantics,
                       premise.error().location.is_valid() ? premise.error().location : law.premise_range.begin,
                       "the precondition of law '" + law.name +
                           "' cannot be stated to the formal core: " + premise.error().reason,
                       explain(premise.error()));
                continue;
            }
            goal = kernel::Proposition::implication(std::move(*premise), std::move(goal));
        }

        std::string unrepresented;
        std::optional<kernel::Proposition> quantified =
            quantify_over(program, law.parameters, std::move(goal), unrepresented);
        if (!quantified.has_value()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, law.range.begin,
                   "law '" + law.name + "' quantifies over '" + unrepresented +
                       "', which the formal core does not represent");
            continue;
        }
        goal = std::move(*quantified);

        Obligation obligation;
        obligation.law = law.id;
        obligation.subject = law.name;
        obligation.origin = Origin::LawProposition;
        obligation.range = law.range;
        obligation.trusted = law.trusted;
        obligation.id = identify(program.context, law.name, goal);
        obligation.goal = std::move(goal);
        program.obligations.push_back(std::move(obligation));
    }

    // A trusted law admitting a memory proposition is an explicit assumption
    // with no kernel proposition, so it is recorded with an identity derived
    // from what it states rather than as an obligation (SPEC.md TRUSTED-003,
    // TRUST.md TCB-TRUST-005). The pointer is identified by its position among
    // the law's parameters. A premise or a count is hashed as written, so
    // renaming a parameter they mention yields a new identity: that errs
    // toward invalidating an assumption, never toward keeping a changed one
    // (TCB-TRUST-006).
    for (const vir::MemoryAssumption& assumption : module.memory_assumptions) {
        source::Hasher hasher;
        hasher.update_field("trusted-memory-proposition-v1");
        hasher.update_field(assumption.name);
        for (const vir::Parameter& parameter : assumption.parameters) {
            hasher.update_field(vir::describe(parameter.type));
        }
        hasher.update_field(assumption.premise.has_value() ? vir::describe(*assumption.premise) : std::string{});
        std::string statement;
        for (const vir::Capability& capability : assumption.capabilities) {
            hasher.update_field(vir::describe(capability.kind));
            hasher.update_field(std::to_string(capability.place.root.id));
            hasher.update_field(capability.extent.empty() ? std::string{} : vir::describe(capability.extent.front()));
            statement += (statement.empty() ? "" : " && ") + vir::describe(capability);
        }
        program.memory_assumptions.push_back(TrustedMemoryAssumption{assumption.name, ObligationId{hasher.finish()},
                                                                     assumption.range.begin, std::move(statement)});
    }

    detail::generate_contracts(module, definitions, program, engine, explain, imports);

    // Direct proves (P) declarations have their own obligations. They never
    // become synthetic Laws or enter the automatic-proof fallback path.
    for (const auto& proof : module.proofs) {
        if (proof.law)
            continue;
        auto proposition = lower_proposition(proof.proposition, program, definitions, proof.parameters.size());
        if (!proposition) {
            report(engine, diagnostics::Category::UnsupportedSemantics, proposition.error().location,
                   "proof '" + proof.name + "' cannot be stated to the formal core: " + proposition.error().reason,
                   explain(proposition.error()));
            continue;
        }
        std::string unrepresented;
        auto goal = quantify_over(program, proof.parameters, std::move(*proposition), unrepresented);
        if (!goal) {
            report(engine, diagnostics::Category::UnsupportedSemantics, proof.range.begin,
                   "proof '" + proof.name + "' quantifies over an unsupported type: " + unrepresented);
            continue;
        }
        Obligation obligation;
        obligation.proof = proof.id;
        obligation.subject = proof.name;
        obligation.origin = Origin::ProofProposition;
        obligation.range = proof.range;
        obligation.id = identify(program.context, "proof:" + proof.name, *goal);
        obligation.goal = std::move(*goal);
        program.obligations.push_back(std::move(obligation));
    }

    lower_proofs(module, elaborated, definitions, program, engine, automation);
    discharge_path_claims(program, engine);
    return program;
}

} // namespace cppl::obligations
