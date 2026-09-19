#include "cppl/automation/evidence.hpp"

#include <variant>

namespace cppl::automation {

namespace {

constexpr std::string_view kDefinitionalStrategy = "definitional-equality";

// The one strategy this implementation has: introduce every quantifier, then
// offer reflexivity. Whether the two sides really are definitionally equal is
// the kernel's decision, made by its own normalizer.
std::optional<kernel::ProofTerm> build(const kernel::Proposition& goal) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&goal.node)) {
        std::optional<kernel::ProofTerm> body = build(*quantified->body);
        if (!body.has_value()) {
            return std::nullopt;
        }
        return kernel::ProofTerm::forall_introduction(quantified->binder, std::move(*body));
    }
    return kernel::ProofTerm::reflexivity();
}

}  // namespace

std::optional<Evidence> propose(const kernel::Context&, const kernel::Proposition& goal) {
    std::optional<kernel::ProofTerm> proof = build(goal);
    if (!proof.has_value()) {
        return std::nullopt;
    }
    return Evidence{std::move(*proof), std::string(kDefinitionalStrategy)};
}

std::vector<obligations::ObligationResult> verify(const obligations::Program& program,
                                                  diagnostics::Engine& engine) {
    std::vector<obligations::ObligationResult> results;
    results.reserve(program.obligations.size());

    for (const obligations::Obligation& obligation : program.obligations) {
        const std::optional<Evidence> evidence = propose(program.context, obligation.goal);

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
            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Error;
            diagnostic.category = evidence.has_value() ? diagnostics::Category::KernelRejection
                                                       : diagnostics::Category::ProofFailure;
            diagnostic.message = "law '" + obligation.law_name + "' is not proven";
            diagnostic.location = obligation.range.begin;
            diagnostic.notes.push_back(
                diagnostics::Note{"goal: " + kernel::describe(obligation.goal),
                                  obligation.range.begin});
            diagnostic.notes.push_back(
                diagnostics::Note{"the kernel did not accept the evidence: " + verdict.reason(),
                                  obligation.range.begin});
            diagnostic.notes.push_back(diagnostics::Note{
                "obligation " + obligation.id.text() + ", status " +
                    obligations::describe(verdict.status()),
                obligation.range.begin});
            engine.report(std::move(diagnostic));
        }

        results.push_back(obligations::ObligationResult{obligation, std::move(verdict),
                                                        std::move(strategy)});
    }

    return results;
}

}  // namespace cppl::automation
