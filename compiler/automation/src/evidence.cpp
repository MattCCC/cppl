#include "cppl/automation/evidence.hpp"

#include "arithmetic.hpp"
#include "composition.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cppl::automation {

// Strategies are tried in a fixed order and each candidate is put to the
// kernel. Trying one proves nothing: only the kernel's acceptance, obtained
// again when the obligation is verified, does.
std::optional<Evidence> propose(const kernel::Context& context, const kernel::Proposition& goal) {
    const auto accepted = [&](const kernel::ProofTerm& proof) {
        return kernel::check(context, goal, proof, kernel::CoreLimits{}).has_value();
    };
    // A disjunctive goal holds by one of its sides, and which one is not decided
    // here: the candidate for each side is offered in turn and the kernel accepts
    // at most the ones that hold. A goal without a disjunction is unaffected,
    // because the two candidates are then identical.
    kernel::ProofTerm definitional = obligations::definitional_evidence(goal);
    if (accepted(definitional)) {
        return Evidence{std::move(definitional), "definitional-equality"};
    }
    kernel::ProofTerm rewritten = obligations::automatic_evidence(goal);
    if (accepted(rewritten)) {
        return Evidence{std::move(rewritten), "premise-and-definitional-equality"};
    }
    for (const auto& proposed :
         {obligations::definitional_evidence(goal, true), obligations::automatic_evidence(goal, true)}) {
        if (accepted(proposed)) {
            return Evidence{proposed, "premise-and-definitional-equality"};
        }
    }
    for (const bool rewriting : {false, true}) {
        if (auto arithmetic = arithmetic_evidence(context, goal, rewriting);
            arithmetic.has_value() && accepted(*arithmetic)) {
            return Evidence{std::move(*arithmetic),
                            rewriting ? "rewriting-and-linear-arithmetic" : "linear-arithmetic"};
        }
    }
    // No candidate holds. The premise-rewriting one is returned so that the
    // kernel's reason for refusing it is what the diagnostic reports.
    return Evidence{std::move(rewritten), "premise-and-definitional-equality"};
}

std::vector<obligations::ObligationResult> verify(const obligations::Program& program, diagnostics::Engine& engine,
                                                  std::size_t* transitions) {
    std::vector<obligations::ObligationResult> results;
    results.reserve(program.obligations.size());
    Composition composition(program);

    for (std::size_t index = 0; index < program.obligations.size(); ++index) {
        const obligations::Obligation& obligation = program.obligations[index];
        // Evidence the author wrote is the evidence submitted. A written proof
        // that the kernel refuses is a failure of that proof; it never falls
        // back to a strategy that might close the goal another way.
        const obligations::WrittenProof* written = program.proof_for(obligation);

        // An explicit assumption is not proved and never looks for evidence
        // (SPEC.md 27). It is recorded as TRUSTED so the trust report can name
        // it. Writing a proof for one is a contradiction in the declaration:
        // the author asked both to assume it and to prove it.
        if (obligation.trusted) {
            if (written != nullptr) {
                diagnostics::Diagnostic diagnostic;
                diagnostic.severity = diagnostics::Severity::Error;
                diagnostic.category = diagnostics::Category::UnsupportedSemantics;
                diagnostic.location = written->range.begin;
                diagnostic.message = "law '" + obligation.subject +
                                     "' is declared trusted, so it is assumed and a "
                                     "written proof for it has nothing to discharge";
                diagnostic.notes.push_back(
                    {"remove 'trusted' to prove it, or remove the proof to assume it", obligation.range.begin});
                engine.report(std::move(diagnostic));
                results.push_back(obligations::ObligationResult{
                    obligation, obligations::Verdict::unresolved("a trusted law has no obligation to discharge"), {}});
                continue;
            }
            results.push_back(obligations::ObligationResult{
                obligation,
                obligations::Verdict::trusted(obligation.range.begin.file + ":" +
                                              std::to_string(obligation.range.begin.line)),
                "explicit trusted assumption"});
            continue;
        }

        // A law whose written proof was refused stays open. The reason was
        // reported where the proof was refused, and the compiler does not go
        // looking for evidence the author did not ask for.
        if (written == nullptr && program.proof_refused(obligation)) {
            results.push_back(obligations::ObligationResult{
                obligation, obligations::Verdict::unresolved("the proof written for this law was refused"), {}});
            continue;
        }

        // Likewise a claim whose written contradiction could not be given
        // evidence: it was reported where that was found, and it is not proven
        // some other way (SPEC.md CASE-005, CASE-015).
        if (obligation.refusal.has_value()) {
            results.push_back(
                obligations::ObligationResult{obligation, obligations::Verdict::unresolved(*obligation.refusal), {}});
            continue;
        }

        std::optional<Evidence> evidence;
        std::string failure = "no strategy in this implementation produced candidate evidence";
        if (composition.owns(index)) {
            // A condition of a verified body, which is proposed evidence only once
            // the contracts it supposes are established. One that carries
            // written evidence - a runtime path claimed not to occur - is offered
            // exactly that evidence and nothing else.
            auto proposed = composition.propose(index);
            if (proposed) {
                evidence = std::move(*proposed);
            } else {
                failure = proposed.error();
            }
        } else if (obligation.evidence.has_value()) {
            // A claim made inside a written proof - an omitted case - carries the
            // evidence its author wrote. Like a written proof, it is submitted as
            // it is and never replaced by a strategy: an impossibility is only
            // ever what the author's contradiction establishes (SPEC.md CASE-005,
            // CASE-015).
            evidence = Evidence{*obligation.evidence, "written contradiction"};
        } else if (written != nullptr) {
            evidence = Evidence{written->term, "written proof '" + written->name + "'"};
        } else if (obligation.origin == obligations::Origin::OmittedCase ||
                   obligation.origin == obligations::Origin::ImpossiblePath) {
            // A claim that a context cannot occur with no written evidence. The
            // facts it rests on may well be contradictory, but that is for the
            // author's contradiction to show, never for a strategy to find.
            failure = "an impossibility is established only by the contradiction written for it";
        } else {
            evidence = propose(program.context, obligation.goal);
        }

        // Written evidence may rest on trusted laws. It is checked relative to
        // exactly those, and the verdict names them (SPEC.md TRUSTED-002); no
        // strategy ever supposes one.
        static const std::vector<obligations::TrustedPremise> outright;
        const std::vector<obligations::TrustedPremise>& premises = obligation.evidence.has_value()
                                                                       ? obligation.assumptions
                                                                   : written != nullptr ? written->assumptions
                                                                                        : outright;

        obligations::Verdict verdict = obligations::Verdict::unresolved(failure);
        std::string strategy;

        if (evidence.has_value()) {
            strategy = evidence->strategy;
            const kernel::CheckResult checked =
                kernel::check(program.context, obligations::relative_to(premises, obligation.goal), evidence->proof,
                              kernel::CoreLimits{});
            if (checked.has_value()) {
                verdict = obligations::Verdict::proven(*checked, obligation, premises);
                if (composition.owns(index)) {
                    auto recorded = composition.accept(index, evidence->proof, *checked);
                    if (!recorded) {
                        verdict = obligations::Verdict::unresolved(recorded.error());
                    }
                }
            } else {
                verdict = obligations::Verdict::unresolved(checked.error().detail);
            }
        }

        if (!verdict.is_proven()) {
            const source::SourceLocation& location = written != nullptr ? written->range.begin : obligation.range.begin;

            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Error;
            diagnostic.category =
                evidence.has_value() ? diagnostics::Category::KernelRejection : diagnostics::Category::ProofFailure;
            // A contract is not a law an author can write a proof for: it is
            // discharged from the function's own body or not at all. The two
            // impossibility claims share one mechanism but are reported under
            // their own names, never as each other (SPEC.md CASE-012, CASE-016).
            if (obligation.origin == obligations::Origin::OmittedCase) {
                diagnostic.message = "omitted " + obligation.subject + " is not shown to be impossible";
            } else if (obligation.origin == obligations::Origin::ImpossiblePath) {
                diagnostic.message = "runtime path '" + obligation.subject + "' is not shown to be unreachable";
            } else if (written != nullptr) {
                diagnostic.message =
                    obligation.proof
                        ? "proof '" + written->name + "' does not establish its proposition"
                        : "proof '" + written->name + "' does not establish law '" + obligation.subject + "'";
            } else if (obligation.origin == obligations::Origin::FunctionContract) {
                diagnostic.message = "verified function '" + obligation.subject + "' does not satisfy its contract";
            } else if (obligation.origin == obligations::Origin::CallPrecondition) {
                diagnostic.message = "call-site precondition for '" + obligation.subject + "' is not proven";
            } else if (obligation.origin == obligations::Origin::ReturnPath) {
                diagnostic.message = "return path '" + obligation.subject + "' does not satisfy its contract";
            } else if (obligation.origin == obligations::Origin::LoopEntry) {
                diagnostic.message = "loop invariant '" + obligation.subject + "' does not hold on entry";
            } else if (obligation.origin == obligations::Origin::LoopPreservation) {
                diagnostic.message = "loop invariant '" + obligation.subject + "' is not preserved by an iteration";
            } else if (obligation.origin == obligations::Origin::LoopDescent) {
                diagnostic.message =
                    "loop measure '" + obligation.subject + "' is not shown to decrease on every iteration";
            } else if (obligation.origin == obligations::Origin::RefinementIntroduction) {
                // The value is what must satisfy the predicate; the refinement is
                // not something a proof can be written for.
                diagnostic.message = "this value is not shown to satisfy refinement type '" +
                                     obligation.subject.substr(obligation.subject.rfind(' ') + 1) + "'";
            } else {
                diagnostic.message = "law '" + obligation.subject + "' is not proven";
            }
            diagnostic.location = location;
            diagnostic.notes.push_back(diagnostics::Note{"goal: " + kernel::describe(obligation.goal), location});
            diagnostic.notes.push_back(
                diagnostics::Note{"the kernel did not accept the evidence: " + verdict.reason(), location});
            diagnostic.notes.push_back(diagnostics::Note{"obligation " + obligation.id.text() + ", status " +
                                                             obligations::describe(verdict.status()),
                                                         location});
            engine.report(std::move(diagnostic));
        }

        results.push_back(obligations::ObligationResult{obligation, std::move(verdict), std::move(strategy)});
    }

    if (transitions != nullptr) {
        *transitions = composition.transitions();
    }
    return results;
}

} // namespace cppl::automation
