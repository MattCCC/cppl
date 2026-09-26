#include "cppl/obligations/trust.hpp"

#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/vir/ids.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
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
        case Origin::CallDescent:
        case Origin::RefinementIntroduction:
        case Origin::ElementBounds:
        case Origin::DefinedBehavior:
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
    // obligation of a verified body contributes to its contract. A claim that a
    // runtime path or a case cannot occur there is decided from the facts of
    // that body, so it rests on the unsafe blocks its contract rests on; which
    // contract that is is kept until those are known.
    std::vector<Premises> contracts(program.contracts.size());
    std::vector<std::pair<std::size_t, std::size_t>> path_claims;
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
            if (kind.has_value()) {
                path_claims.emplace_back(closure.claims.size() - 1, contract->second);
            }
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

    // The unsafe blocks each contract rests on travel the same edges: a caller
    // proven from a callee's contract rests on whatever that contract rests on
    // (TRUST.md TCB-REPORT-005). The same fixed point, over locations.
    using Regions = std::map<std::tuple<std::string, std::uint32_t, std::uint32_t>, UnsafeDependency>;
    const auto key = [](const source::SourceLocation& at) {
        return std::tuple{at.file, at.line, at.column};
    };
    std::vector<Regions> regions(program.contracts.size());
    for (std::size_t index = 0; index < program.contracts.size(); ++index) {
        for (const source::SourceLocation& at : program.contracts[index].unsafe_regions) {
            regions[index].emplace(key(at), UnsafeDependency{at, true});
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t index = 0; index < regions.size(); ++index) {
            for (const std::size_t callee : callees[index]) {
                if (callee == index) {
                    continue;
                }
                for (const auto& [where, region] : regions[callee]) {
                    changed = regions[index].emplace(where, UnsafeDependency{region.location, false}).second || changed;
                }
            }
        }
    }

    const auto listed = [](const Regions& found) {
        std::vector<UnsafeDependency> unsafe;
        unsafe.reserve(found.size());
        for (const auto& [where, region] : found) {
            unsafe.push_back(region);
        }
        return unsafe;
    };
    // The contracts of other units each contract was proven through travel the
    // same edges: its own calls' first, then those of every contract it calls
    // (SPEC.md TUBOUND-006). Keyed by contract index, marked direct where the body
    // itself makes the call.
    std::vector<std::map<std::size_t, bool>> through(program.contracts.size());
    for (std::size_t index = 0; index < program.contracts.size(); ++index) {
        for (const std::size_t callee : callees[index]) {
            if (program.contracts[callee].imported.has_value()) {
                through[index][callee] = true;
            }
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t index = 0; index < through.size(); ++index) {
            for (const std::size_t callee : callees[index]) {
                if (callee == index) {
                    continue;
                }
                for (const auto& [imported, direct] : through[callee]) {
                    changed = through[index].emplace(imported, false).second || changed;
                }
            }
        }
    }
    const auto imported_dependency = [&program](std::size_t index, bool direct) {
        const ContractVerification& contract = program.contracts[index];
        const ImportedContract& recorded = *contract.imported;
        return ImportedDependency{contract.name,     contract.symbol, recorded.origin,  recorded.entry, contract.total,
                                  recorded.premises, recorded.unsafe, recorded.depends, direct};
    };
    const auto imported_list = [&](const std::map<std::size_t, bool>& found) {
        std::vector<ImportedDependency> imported;
        imported.reserve(found.size());
        for (const auto& [index, direct] : found) {
            imported.push_back(imported_dependency(index, direct));
        }
        std::ranges::sort(imported, {}, &ImportedDependency::symbol);
        return imported;
    };

    // The standard-library models each contract rests on travel the same
    // edges: a caller proven from a callee's contract rests on what the
    // callee's model states as surely as the callee does (SPEC.md
    // STDMODEL-018, TRUST.md 28.1). The same fixed point, over models.
    using Models = std::map<source::RepresentationKind, LibraryDependency>;
    std::vector<Models> models(program.contracts.size());
    for (std::size_t index = 0; index < program.contracts.size(); ++index) {
        for (const source::RepresentationKind model : program.contracts[index].library_models) {
            models[index].emplace(model, LibraryDependency{model, true});
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t index = 0; index < models.size(); ++index) {
            for (const std::size_t callee : callees[index]) {
                if (callee == index) {
                    continue;
                }
                for (const auto& [model, dependency] : models[callee]) {
                    changed = models[index].emplace(model, LibraryDependency{model, false}).second || changed;
                }
            }
        }
    }
    const auto listed_models = [](const Models& found) {
        std::vector<LibraryDependency> library;
        library.reserve(found.size());
        for (const auto& [model, dependency] : found) {
            library.push_back(dependency);
        }
        return library;
    };

    for (const auto& [claim, contract] : path_claims) {
        closure.claims[claim].unsafe = listed(regions[contract]);
        closure.claims[claim].imported = imported_list(through[contract]);
        closure.claims[claim].library = listed_models(models[contract]);
    }

    // A contract of a recursion group holds only with the whole group, each
    // member's proof supposing the others' as its induction hypothesis.
    const auto conditions_proven = [&](const ContractVerification& contract) {
        return std::ranges::all_of(contract.conditions, [&results](const VerificationCondition& condition) {
            return results[condition.obligation].verdict.is_proven();
        });
    };
    for (std::size_t index = 0; index < program.contracts.size(); ++index) {
        const ContractVerification& contract = program.contracts[index];
        // Another unit's contract is no claim of this one: nothing here proved
        // it. It is listed so the report names every contract assumed from an
        // interface, and every claim of this unit that rests on it names it.
        if (contract.imported.has_value()) {
            closure.imports.push_back(imported_dependency(index, false));
            continue;
        }
        const bool proven = contract.partial
                                ? conditions_proven(contract) &&
                                      std::ranges::all_of(contract.recursion,
                                                          [&](std::size_t member) {
                                                              return member < program.contracts.size() &&
                                                                     conditions_proven(program.contracts[member]);
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
            ordered(std::move(contracts[index])), listed(regions[index]), contract.total, imported_list(through[index]),
            contract.symbol, listed_models(models[index])});
    }

    return closure;
}

bool rests_on_trusted_laws(const ClaimClosure& claim) {
    return !claim.premises.empty() || std::ranges::any_of(claim.imported, [](const ImportedDependency& imported) {
        return !imported.premises.empty();
    });
}

bool rests_on_unsafe_code(const ClaimClosure& claim) {
    return !claim.unsafe.empty() || std::ranges::any_of(claim.imported, [](const ImportedDependency& imported) {
        return !imported.unsafe.empty();
    });
}

} // namespace cppl::obligations
