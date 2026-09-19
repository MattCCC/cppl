#pragma once

#include "cppl/automation/evidence.hpp"

#include <expected>
#include <map>
#include <set>

namespace cppl::automation {

class Composition {
  public:
    explicit Composition(const obligations::Program& program);
    [[nodiscard]] bool owns(std::size_t obligation) const;
    [[nodiscard]] std::expected<Evidence, std::string> propose(std::size_t obligation) const;
    [[nodiscard]] std::expected<void, std::string> accept(std::size_t obligation, const kernel::ProofTerm& proof,
                                                          const kernel::Acceptance& acceptance);

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
};

} // namespace cppl::automation
