#pragma once

#include <expected>
#include <map>

#include "cppl/automation/evidence.hpp"

namespace cppl::automation {

class Composition {
public:
    explicit Composition(const obligations::Program& program);
    [[nodiscard]] bool owns(std::size_t obligation) const;
    [[nodiscard]] std::expected<Evidence, std::string> propose(std::size_t obligation) const;
    [[nodiscard]] std::expected<void, std::string> accept(
        std::size_t obligation, const kernel::ProofTerm& proof, const kernel::Acceptance& acceptance);

private:
    struct Stage {
        const obligations::ContractVerification* function;
        std::size_t prefix;
        const kernel::Proposition* reasoning;
        bool final;
    };
    struct Theorem {
        kernel::Proposition goal;
        kernel::ProofTerm proof;
    };
    const obligations::Program& program_;
    std::map<std::size_t, Stage> stages_;
    std::map<std::size_t, kernel::ProofTerm> proven_;
    std::map<std::uint32_t, Theorem> callees_;
};

}  // namespace cppl::automation
