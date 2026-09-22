#pragma once

#include "cppl/automation/evidence.hpp"

#include <expected>
#include <map>
#include <optional>
#include <set>

namespace cppl::automation {

class Composition {
  public:
    explicit Composition(const obligations::Program& program);
    [[nodiscard]] bool owns(std::size_t obligation) const;
    [[nodiscard]] std::expected<Evidence, std::string> propose(std::size_t obligation) const;
    [[nodiscard]] std::expected<void, std::string> accept(std::size_t obligation, const kernel::ProofTerm& proof,
                                                          const kernel::Acceptance& acceptance);

    // Search transitions charged so far. Composing one obligation's evidence
    // costs a transition per dependency it consumes, so a program's whole cost
    // is bounded by its own size. The count is deterministic: the same program
    // costs the same on every machine, unlike elapsed time.
    [[nodiscard]] std::size_t transitions() const {
        return transitions_;
    }
    [[nodiscard]] static std::size_t budget_for(const obligations::Program& program);

    // Charges one transition, or reports that the search cannot continue.
    //
    // A transition consumes a dependency's evidence, so that dependency must
    // already be proven. Reaching an unproven one means the search advanced
    // into a state it had no evidence to enter, and it stops there rather than
    // reading evidence that does not exist.
    //
    // Public because it carries its own contract, which is stated directly by
    // `an_unproven_dependency_is_refused_before_its_evidence_is_read` rather
    // than only through the callers that happen to charge it.
    [[nodiscard]] std::expected<void, std::string> spend(std::optional<std::size_t> dependency = std::nullopt) const;

  private:
    struct Stage {
        const obligations::ContractVerification* function;
        const obligations::ReturnPath* path;
        std::size_t prefix;
        std::size_t conditions;
        const kernel::Proposition* reasoning;
        bool path_end;
    };
    struct Theorem {
        kernel::Proposition goal;
        kernel::ProofTerm proof;
    };
    // A verification condition of a partial-correctness contract.
    struct Condition {
        std::size_t contract;
        const obligations::VerificationCondition* condition;
    };

    [[nodiscard]] bool established(std::size_t contract) const;
    [[nodiscard]] std::expected<Evidence, std::string> propose_condition(const Condition& condition,
                                                                         std::size_t obligation) const;

    const obligations::Program& program_;
    std::map<std::size_t, Stage> stages_;
    std::map<std::size_t, Condition> conditions_;
    std::map<std::size_t, kernel::ProofTerm> proven_;
    std::map<std::uint32_t, Theorem> callees_;
    std::set<std::size_t> partial_established_;
    // Mutable because proposing evidence is logically a query: it answers what
    // evidence exists without changing what has been proven. The budget it
    // spends is bookkeeping about the search, not part of that answer.
    mutable std::size_t transitions_ = 0;
    std::size_t budget_ = 0;
};

} // namespace cppl::automation
