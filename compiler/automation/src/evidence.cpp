#include "cppl/automation/evidence.hpp"

#include "arithmetic.hpp"
#include "composition.hpp"

#include <variant>

namespace cppl::automation {

// Strategies are tried in a fixed order and each candidate is put to the
// kernel. Trying one proves nothing: only the kernel's acceptance, obtained
// again when the obligation is verified, does.
std::optional<Evidence> propose(const kernel::Context& context, const kernel::Proposition& goal) {
    const auto accepted = [&](const kernel::ProofTerm& proof) {
        return kernel::check(context, goal, proof, kernel::CoreLimits{}).has_value();
    };
    kernel::ProofTerm definitional = obligations::definitional_evidence(goal);
    if (accepted(definitional)) {
        return Evidence{std::move(definitional), "definitional-equality"};
    }
    kernel::ProofTerm rewritten = obligations::automatic_evidence(goal);
    if (accepted(rewritten)) {
        return Evidence{std::move(rewritten), "premise-and-definitional-equality"};
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

std::vector<obligations::ObligationResult> verify(const obligations::Program& program, diagnostics::Engine& engine) {
    std::vector<obligations::ObligationResult> results;
    results.reserve(program.obligations.size());
    Composition composition(program);

    for (std::size_t index = 0; index < program.obligations.size(); ++index) {
        const obligations::Obligation& obligation = program.obligations[index];
        // Evidence the author wrote is the evidence submitted. A written proof
        // that the kernel refuses is a failure of that proof; it never falls
        // back to a strategy that might close the goal another way.
        const obligations::WrittenProof* written = program.proof_for(obligation);

        // A law whose written proof was refused stays open. The reason was
        // reported where the proof was refused, and the compiler does not go
        // looking for evidence the author did not ask for.
        if (written == nullptr && program.proof_refused(obligation)) {
            results.push_back(obligations::ObligationResult{
                obligation, obligations::Verdict::unresolved("the proof written for this law was refused"), {}});
            continue;
        }

        std::optional<Evidence> evidence;
        std::string failure = "no strategy in this implementation produced candidate evidence";
        if (written != nullptr) {
            evidence = Evidence{written->term, "written proof '" + written->name + "'"};
        } else if (composition.owns(index)) {
            auto proposed = composition.propose(index);
            if (proposed) {
                evidence = std::move(*proposed);
            } else {
                failure = proposed.error();
            }
        } else {
            evidence = propose(program.context, obligation.goal);
        }

        obligations::Verdict verdict = obligations::Verdict::unresolved(failure);
        std::string strategy;

        if (evidence.has_value()) {
            strategy = evidence->strategy;
            const kernel::CheckResult checked =
                kernel::check(program.context, obligation.goal, evidence->proof, kernel::CoreLimits{});
            if (checked.has_value()) {
                verdict = obligations::Verdict::proven(*checked, obligation);
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
            // discharged from the function's own body or not at all.
            if (written != nullptr) {
                diagnostic.message =
                    "proof '" + written->name + "' does not establish law '" + obligation.subject + "'";
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

    return results;
}

} // namespace cppl::automation
