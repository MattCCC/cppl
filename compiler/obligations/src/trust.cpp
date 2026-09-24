#include "cppl/obligations/trust.hpp"

#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/ids.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cppl::obligations {

namespace {

using Premises = std::map<vir::LawId, TrustedPremise>;

// A premise joined twice stays direct if either occurrence names it directly.
bool join(Premises& into, const TrustedPremise& premise, bool direct) {
    auto [entry, added] = into.emplace(premise.law, premise);
    if (added) {
        entry->second.direct = direct;
        return true;
    }
    entry->second.direct = entry->second.direct || direct;
    return false;
}

std::vector<TrustedPremise> ordered(Premises premises) {
    std::vector<TrustedPremise> result;
    result.reserve(premises.size());
    for (auto& [law, premise] : premises) {
        result.push_back(std::move(premise));
    }
    return result;
}

std::optional<ClaimKind> claim_of(Origin origin) {
    switch (origin) {
        case Origin::LawProposition:
            return ClaimKind::Law;
        case Origin::ProofProposition:
            return ClaimKind::Proof;
        case Origin::OmittedCase:
            return ClaimKind::OmittedCase;
        case Origin::ImpossiblePath:
            return ClaimKind::ImpossiblePath;
        case Origin::FunctionContract:
        case Origin::CallPrecondition:
        case Origin::ReturnPath:
        case Origin::LoopEntry:
        case Origin::LoopPreservation:
        case Origin::LoopDescent:
        case Origin::RefinementIntroduction:
        case Origin::ElementBounds:
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

std::string describe(ClaimKind kind) {
    switch (kind) {
        case ClaimKind::Law:
            return "law";
        case ClaimKind::Proof:
            return "proof";
        case ClaimKind::LawInstance:
            return "proof of a law instance";
        case ClaimKind::Contract:
            return "contract of";
        case ClaimKind::OmittedCase:
            return "omitted";
        case ClaimKind::ImpossiblePath:
            return "unreachable runtime path";
    }
    return "claim";
}

TrustClosure close_trust(const Program& program, const std::vector<ObligationResult>& results) {
    TrustClosure closure;
    closure.memory_assumptions = program.memory_assumptions;

    if (results.size() != program.obligations.size()) {
        closure.faults.emplace_back("the verification results do not correspond to the obligations");
        return closure;
    }

    std::set<vir::LawId> declared;
    for (const Obligation& obligation : program.obligations) {
        if (obligation.trusted && obligation.law.has_value()) {
            declared.insert(*obligation.law);
            closure.assumptions.push_back(TrustedPremise{*obligation.law, obligation.subject, obligation.id,
                                                         obligation.range.begin, obligation.goal});
        }
    }
    std::ranges::sort(closure.assumptions, {}, &TrustedPremise::law);

    // A premise must name an explicit assumption of this unit. Anything else
    // would be reported as trust it is not, or hide what it really is.
    const auto admitted = [&](const std::string& claim, const std::vector<TrustedPremise>& premises) {
        for (const TrustedPremise& premise : premises) {
            if (!declared.contains(premise.law)) {
                closure.faults.push_back(claim + " rests on '" + premise.name +
                                         "', which is not a trusted law of this translation unit");
            }
        }
    };

    // Which contract each obligation of a verified body belongs to, and which
    // contract states each function.
    std::map<std::size_t, std::size_t> owner;
    std::map<std::uint32_t, std::size_t> contract_of;
    const auto own = [&](std::size_t obligation, std::size_t contract) {
        const auto [entry, added] = owner.emplace(obligation, contract);
        if (!added && entry->second != contract) {
            closure.faults.push_back("obligation " + program.obligations[obligation].id.text() +
                                     " belongs to the contracts of both '" + program.contracts[entry->second].name +
                                     "' and '" + program.contracts[contract].name + "'");
        }
    };
    for (std::size_t index = 0; index < program.contracts.size(); ++index) {
        const ContractVerification& contract = program.contracts[index];
        if (!contract_of.emplace(contract.function.value, index).second) {
            closure.faults.push_back("function '" + contract.name + "' has more than one contract");
        }
        if (contract.partial) {
            for (const VerificationCondition& condition : contract.conditions) {
                own(condition.obligation, index);
            }
            continue;
        }
        own(contract.obligation, index);
        for (const ReturnPath& path : contract.paths) {
            own(path.obligation, index);
            for (const CallVerification& call : path.calls) {
                for (const CallPrecondition& precondition : call.preconditions) {
                    own(precondition.obligation, index);
                }
            }
        }
    }
    for (const auto& [obligation, contract] : owner) {
        if (obligation >= results.size()) {
            closure.faults.push_back("the contract of '" + program.contracts[contract].name +
                                     "' names an obligation that does not exist");
        }
    }
    if (!closure.faults.empty()) {
        return closure;
    }

    // Claims that stand on their own, in program order, and what each
    // obligation of a verified body contributes to its contract.
    std::vector<Premises> contracts(program.contracts.size());
    for (std::size_t index = 0; index < results.size(); ++index) {
        const ObligationResult& result = results[index];
        if (!result.verdict.is_proven()) {
            continue;
        }
        const Obligation& obligation = program.obligations[index];
        const std::vector<TrustedPremise>& premises = result.verdict.premises();
        const std::optional<ClaimKind> kind = claim_of(obligation.origin);
        const std::string claim = describe(obligation.origin) + " '" + obligation.subject + "'";
        admitted(claim, premises);

        if (kind.has_value()) {
            closure.claims.push_back(
                ClaimClosure{*kind, obligation.subject, obligation.range.begin, obligation.id, premises});
        }
        if (const auto contract = owner.find(index); contract != owner.end()) {
            for (const TrustedPremise& premise : premises) {
                join(contracts[contract->second], premise, premise.direct);
            }
        } else if (!kind.has_value() && !premises.empty()) {
            closure.faults.push_back(claim + " rests on trusted laws but belongs to no claim this report names");
        }
    }

    // A proof of one law instance was checked where it was lowered, relative
    // to the premises it carries; it has no obligation of its own.
    for (const WrittenProof& written : program.proofs) {
        if (!written.law.has_value() || written.closes_law) {
            continue;
        }
        admitted("proof '" + written.name + "'", written.assumptions);
        closure.claims.push_back(ClaimClosure{ClaimKind::LawInstance, written.name, written.range.begin,
                                              detail::identify_goal(program.context, "proof:" + written.name,
                                                                    relative_to(written.assumptions, written.goal)),
                                              written.assumptions});
    }

    // Each contract rests on what the contracts it calls rest on (TRUST.md
    // TCB-PROV-006). Joining only ever adds a law to a contract, and there are
    // finitely many of both, so this reaches a fixed point whatever cycles the
    // call graph has (TCB-PROV-005).
    std::vector<std::set<std::size_t>> callees(program.contracts.size());
    std::vector<std::string> unknown(program.contracts.size());
    for (std::size_t index = 0; index < program.contracts.size(); ++index) {
        const ContractVerification& contract = program.contracts[index];
        for (const ReturnPath& path : contract.paths) {
            for (const CallVerification& call : path.calls) {
                if (const auto callee = contract_of.find(call.callee.value); callee != contract_of.end()) {
                    callees[index].insert(callee->second);
                } else if (unknown[index].empty()) {
                    unknown[index] = call.callee_name;
                }
            }
        }
        for (const VerificationCondition& condition : contract.conditions) {
            for (const std::size_t callee : condition.callees) {
                if (callee < program.contracts.size()) {
                    callees[index].insert(callee);
                } else if (unknown[index].empty()) {
                    unknown[index] = "a function outside this translation unit";
                }
            }
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t index = 0; index < contracts.size(); ++index) {
            for (const std::size_t callee : callees[index]) {
                if (callee == index) {
                    continue;
                }
                for (const auto& [law, premise] : contracts[callee]) {
                    changed = join(contracts[index], premise, false) || changed;
                }
            }
        }
    }

    for (std::size_t index = 0; index < program.contracts.size(); ++index) {
        const ContractVerification& contract = program.contracts[index];
        const bool proven = contract.partial
                                ? std::ranges::all_of(contract.conditions,
                                                      [&results](const VerificationCondition& condition) {
                                                          return results[condition.obligation].verdict.is_proven();
                                                      })
                                : results[contract.obligation].verdict.is_proven();
        if (!proven) {
            continue;
        }
        // A contract proven while calling a function whose contract is not
        // here would rest on assumptions no one can list.
        if (!unknown[index].empty()) {
            closure.faults.push_back("the contract of '" + contract.name + "' is proven through a call to '" +
                                     unknown[index] + "', whose trusted laws cannot be known");
            continue;
        }
        const std::size_t anchor =
            contract.partial ? (contract.conditions.empty() ? results.size() : contract.conditions.front().obligation)
                             : contract.obligation;
        closure.claims.push_back(ClaimClosure{
            ClaimKind::Contract, contract.name,
            anchor < results.size() ? program.obligations[anchor].range.begin : source::SourceLocation{},
            contract.partial ? ObligationId{contract.identity} : program.obligations[contract.obligation].id,
            ordered(std::move(contracts[index]))});
    }

    return closure;
}

} // namespace cppl::obligations
