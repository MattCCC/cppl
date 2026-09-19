#include "cppl/automation/evidence.hpp"

#include <variant>

namespace cppl::automation {

namespace {

constexpr std::string_view kDefinitionalStrategy = "definitional-equality";

}  // namespace

std::optional<Evidence> propose(const kernel::Context&, const kernel::Proposition& goal) {
    return Evidence{obligations::definitional_evidence(goal), std::string(kDefinitionalStrategy)};
}

std::vector<obligations::ObligationResult> verify(const obligations::Program& program,
                                                  diagnostics::Engine& engine) {
    std::vector<obligations::ObligationResult> results;
    results.reserve(program.obligations.size());

    for (const obligations::Obligation& obligation : program.obligations) {
        // Evidence the author wrote is the evidence submitted. A written proof
        // that the kernel refuses is a failure of that proof; it never falls
        // back to a strategy that might close the goal another way.
        const obligations::WrittenProof* written = program.proof_for(obligation.law);

        // A law whose written proof was refused stays open. The reason was
        // reported where the proof was refused, and the compiler does not go
        // looking for evidence the author did not ask for.
        if (written == nullptr && program.proof_refused(obligation.law)) {
            results.push_back(obligations::ObligationResult{
                obligation,
                obligations::Verdict::unresolved("the proof written for this law was refused"),
                {}});
            continue;
        }

        const std::optional<Evidence> evidence =
            written != nullptr
                ? std::optional<Evidence>{Evidence{written->term,
                                                   "written proof '" + written->name + "'"}}
                : propose(program.context, obligation.goal);

        obligations::Verdict verdict = obligations::Verdict::unresolved(
            "no strategy in this implementation produced candidate evidence");
        std::string strategy;

        if (evidence.has_value()) {
            strategy = evidence->strategy;
            const kernel::CheckResult checked = kernel::check(program.context, obligation.goal,
                                                              evidence->proof, kernel::CoreLimits{});
            if (checked.has_value()) {
                verdict = obligations::Verdict::proven(*checked, obligation);
            } else {
                verdict = obligations::Verdict::unresolved(checked.error().detail);
            }
        }

        if (!verdict.is_proven()) {
            const source::SourceLocation& location =
                written != nullptr ? written->range.begin : obligation.range.begin;

            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Error;
            diagnostic.category = evidence.has_value() ? diagnostics::Category::KernelRejection
                                                       : diagnostics::Category::ProofFailure;
            diagnostic.message =
                written != nullptr
                    ? "proof '" + written->name + "' does not establish law '" +
                          obligation.law_name + "'"
                    : "law '" + obligation.law_name + "' is not proven";
            diagnostic.location = location;
            diagnostic.notes.push_back(
                diagnostics::Note{"goal: " + kernel::describe(obligation.goal), location});
            diagnostic.notes.push_back(
                diagnostics::Note{"the kernel did not accept the evidence: " + verdict.reason(),
                                  location});
            diagnostic.notes.push_back(diagnostics::Note{
                "obligation " + obligation.id.text() + ", status " +
                    obligations::describe(verdict.status()),
                location});
            engine.report(std::move(diagnostic));
        }

        results.push_back(obligations::ObligationResult{obligation, std::move(verdict),
                                                        std::move(strategy)});
    }

    return results;
}

}  // namespace cppl::automation
