#include "cppl/obligations/contracts.hpp"

#include "aggregates.hpp"
#include "contracts_conditions.hpp"
#include "cppl/artifact/interface.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/obligations/interface.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/types.hpp"
#include "definedness.hpp"
#include "library.hpp"
#include "lowering.hpp"
#include "walks.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cppl::obligations::detail {

using contracts::build;
using contracts::build_group;
using contracts::build_partial;
using contracts::membership;
using contracts::recursion_groups;
using contracts::report;
using contracts::report_termination;
using contracts::settle_totality;
using contracts::state_contract;

namespace {

// Why a contract of another unit, or a call relying on one, is not used here.
struct Refusal {
    std::string message;
    std::vector<std::string> notes;
    source::SourceLocation location;
};

void report(diagnostics::Engine& engine, const Refusal& refusal) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = diagnostics::Category::VerificationInterface;
    diagnostic.location = refusal.location;
    diagnostic.message = refusal.message;
    for (const std::string& note : refusal.notes) {
        diagnostic.notes.push_back({note, refusal.location});
    }
    engine.report(std::move(diagnostic));
}

// Whether every later `verified` declaration of a function states the contract
// its first one does (SPEC.md TU-003). They are compared by meaning, as a
// contract of another unit is, so renamed parameters and respelled clauses
// agree, and a conflict is refused rather than one of them silently used.
bool restatements_agree(const vir::Function& function, const DefinitionMap& pure_definitions, const Program& program,
                        diagnostics::Engine& engine) {
    if (function.redeclared_contracts.empty() || !function.contract.has_value()) {
        return true;
    }
    const auto stated = [&](const vir::Contract& contract) -> std::optional<Statement> {
        vir::Function declared;
        declared.id = function.id;
        declared.symbol = function.symbol;
        declared.qualified_name = function.qualified_name;
        declared.parameters = function.parameters;
        declared.result = function.result;
        declared.contract = contract;
        declared.range = function.range;
        // Compared whatever the linkage: this is one entity within one unit.
        declared.external_linkage = true;
        ContractVerification plan;
        if (!state_contract(declared, pure_definitions, program, plan) || !plan.statement.has_value()) {
            return std::nullopt;
        }
        return Statement{*plan.statement, plan.description};
    };
    const std::optional<Statement> first = stated(*function.contract);
    for (const vir::Contract& other : function.redeclared_contracts) {
        const std::optional<Statement> restated = stated(other);
        if (first.has_value() && restated.has_value() && first->identity == restated->identity) {
            continue;
        }
        diagnostics::Diagnostic diagnostic;
        diagnostic.severity = diagnostics::Severity::Error;
        diagnostic.category = diagnostics::Category::Elaboration;
        diagnostic.location = other.range.begin.is_valid() ? other.range.begin : function.range.begin;
        diagnostic.message = "this verified declaration of '" + function.qualified_name +
                             "' states a different contract from its earlier one";
        diagnostic.notes.push_back(
            {"earlier: " + (first.has_value() ? first->description : std::string("a contract that cannot be stated")),
             function.contract->range.begin});
        diagnostic.notes.push_back({"here: " + (restated.has_value() ? restated->description
                                                                     : std::string("a contract that cannot be stated")),
                                    diagnostic.location});
        diagnostic.notes.push_back({"the declarations of one function state one contract, and a conflicting one is "
                                    "ill-formed (SPEC.md TU-003)",
                                    diagnostic.location});
        engine.report(std::move(diagnostic));
        return false;
    }
    return true;
}

// The contract of a verified function this unit declares and does not define
// (SPEC.md TUBOUND-003, TUBOUND-004).
//
// The contract is stated here, from this unit's own declaration, exactly as it
// would be for a function defined here. An interface entry is consulted only to
// learn that another unit proved that same statement for that same callable:
// its statement identity must equal the one built here. The proposition the
// caller supposes is therefore always the one this unit stated; nothing is read
// out of the entry as a proposition. The entry supplies only what a proof
// elsewhere rests on, and whether it is total.
std::expected<ContractVerification, Refusal> import_contract(const vir::Function& function,
                                                             const DefinitionMap& pure_definitions,
                                                             const Program& program, const Imports& imports) {
    const std::string declared =
        "verified function '" + function.qualified_name + "' is declared but not defined in this translation unit";
    const source::SourceLocation& location = function.range.begin;
    ContractVerification plan;
    if (auto stated = state_contract(function, pure_definitions, program, plan); !stated) {
        return std::unexpected(
            Refusal{declared + ", and its contract cannot be stated to the formal core here: " + stated.error().reason,
                    {"a contract of another unit is used only as this unit states it (SPEC.md "
                     "TUBOUND-004)"},
                    location});
    }
    const ImportedEntry* recorded = imports.find(function.symbol.usr);
    if (recorded == nullptr) {
        if (const RefusedEntry* refused = imports.refusal(function.symbol.usr)) {
            return std::unexpected(Refusal{declared + ", and the contract '" + refused->origin +
                                               "' records for it cannot be used: " + refused->reason,
                                           {"a contract of another unit is used only from a verification interface "
                                            "that is intact, current and produced under this unit's configuration "
                                            "(SPEC.md TUBOUND-005)"},
                                           location});
        }
        return std::unexpected(Refusal{
            declared + ", and no imported verification interface records its contract",
            {"a declaration is not evidence: compile the unit that defines it with '--cppl-emit-interface=<file>' "
             "and name that file here with '--cppl-import-interface=<file>' (SPEC.md TUBOUND-003, TUBOUND-001)"},
            location});
    }
    if (!plan.interface_statement.has_value()) {
        return std::unexpected(Refusal{"the contract of '" + function.qualified_name +
                                           "' cannot be identified here, so it cannot be compared with the one '" +
                                           recorded->origin + "' records",
                                       {"a contract of another unit is used only where this unit states the same "
                                        "one (SPEC.md TUBOUND-004)"},
                                       location});
    }
    if (!(*plan.interface_statement == recorded->entry.statement)) {
        return std::unexpected(Refusal{"the contract this translation unit declares for '" + function.qualified_name +
                                           "' is not the one '" + recorded->origin + "' records as verified",
                                       {"declared here: " + plan.description,
                                        "recorded there: " + artifact::displayed(recorded->entry.contract),
                                        "each unit states a contract from its own declaration, and another unit's "
                                        "proof is used only where the two agree (SPEC.md TUBOUND-004)"},
                                       location});
    }
    const bool total = recorded->entry.correctness == artifact::Correctness::Total;
    // Asking that a function terminate is part of its statement, so a unit that
    // proved it could not have recorded it partial; a record that does is not
    // one this compiler wrote, and is not believed.
    if (!total && function.contract.has_value() && !function.contract->measures.empty()) {
        return std::unexpected(Refusal{"'" + recorded->origin + "' records the contract of '" +
                                           function.qualified_name +
                                           "' as partial correctness, but its declaration asks that it terminate",
                                       {"a function stating 'decreases' is verified total or not at all (SPEC.md "
                                        "TERMINATION-006, TUBOUND-007)"},
                                       location});
    }
    plan.partial = true;
    plan.total = total;
    source::Hasher identity;
    identity.update_field("imported-contract-v1");
    identity.update_field(recorded->identity.to_hex());
    plan.identity = identity.finish();
    plan.imported = ImportedContract{recorded->origin,       recorded->identity,     recorded->entry.premises,
                                     recorded->entry.models, recorded->entry.unsafe, recorded->entry.depends,
                                     recorded->entry.runtime};
    return plan;
}

} // namespace

void generate_contracts(const vir::Module& module, const DefinitionMap& pure_definitions, Program& program,
                        diagnostics::Engine& engine, const std::function<std::string(const Failure&)>& explain,
                        const Imports& imports) {
    Contracts contracts;
    // Only a function that states a contract and has a return tree is a
    // candidate. Pairing the tree with the function carries that guarantee
    // onward instead of re-asserting it at every use.
    struct Candidate {
        const vir::Function* function;
        const vir::Expr* returned_value;
        std::vector<const vir::Expr*> calls;            // every verified call the body makes
        std::vector<std::size_t> callees;               // the candidates those calls reach, each once
        std::vector<std::string> external = {};         // the functions defined elsewhere they reach, each once
        bool library = false;                           // whether the body calls a library summary
        bool validates = false;                         // whether the body holds a validation expression
        std::optional<Failure> unstated = std::nullopt; // a library call no summary could be stated for
    };
    std::vector<Candidate> candidates;
    // A verified function declared here and defined in another unit states a
    // contract too, which a caller may use once an interface establishes it
    // (SPEC.md TUBOUND-003). It has no body, so it is never a candidate.
    std::vector<const vir::Function*> externals;
    for (const auto& function : module.functions) {
        if (!restatements_agree(function, pure_definitions, program, engine)) {
            continue; // refused, so no caller relies on either statement
        }
        if (function.contract.has_value() && function.returned_value.has_value()) {
            contracts.emplace(function.symbol.usr, &function);
            candidates.push_back({&function, &function.returned_value.value(), {}, {}, {}, false, false, std::nullopt});
        } else if (function.contract.has_value() && function.defined_elsewhere) {
            contracts.emplace(function.symbol.usr, &function);
            externals.push_back(&function);
        }
    }
    std::map<std::string, std::size_t> position_of;
    for (std::size_t position = 0; position < candidates.size(); ++position) {
        position_of.emplace(candidates[position].function->symbol.usr, position);
    }
    // Every library operation a body calls is stated once, from its trusted
    // summary, before any body is built (RFC 0020 §6). A call no summary can be
    // stated for fails where it is made, with the reason.
    for (Candidate& candidate : candidates) {
        collect_calls(*candidate.returned_value, contracts, candidate.calls);
        std::erase_if(candidate.calls, [&](const vir::Expr* site) {
            const auto& call = std::get<vir::Call>(site->node);
            // A validation calls no function of the program: the test it
            // performs is supposed where the path makes it (SPEC.md
            // RUNTIMECHECK-011).
            if (call.validation.has_value()) {
                candidate.validates = true;
                return true;
            }
            if (!call.library.has_value()) {
                return false;
            }
            candidate.library = true;
            if (program.library_summary(call.callee.usr) == nullptr) {
                auto summary = library_summary(call, *site);
                if (summary) {
                    program.library.push_back(std::move(*summary));
                } else if (!candidate.unstated.has_value()) {
                    candidate.unstated = summary.error();
                }
            }
            return true;
        });
        for (const vir::Expr* site : candidate.calls) {
            const std::string& usr = std::get<vir::Call>(site->node).callee.usr;
            if (const auto callee = position_of.find(usr); callee != position_of.end()) {
                if (std::ranges::find(candidate.callees, callee->second) == candidate.callees.end()) {
                    candidate.callees.push_back(callee->second);
                }
            } else if (std::ranges::find(candidate.external, usr) == candidate.external.end()) {
                candidate.external.push_back(usr);
            }
        }
    }

    // The recursion groups: functions that reach one another through their
    // calls, found as the strongly connected components of the call graph.
    const std::vector<std::vector<std::size_t>> groups =
        recursion_groups(candidates.size(), [&candidates](std::size_t position) -> const std::vector<std::size_t>& {
            return candidates[position].callees;
        });

    // A group is recursive when it has more than one member or its one member
    // calls itself. Recursion is verified only with a measure every call within
    // the group descends (SPEC.md TERMINATION-007), and a group whose members
    // do not all state one, of one length, is refused before anything is built.
    struct Unit {
        std::vector<std::size_t> members;
        bool recursive = false;
    };
    std::vector<Unit> pending;
    for (const std::vector<std::size_t>& group : groups) {
        const bool recursive =
            group.size() > 1 || std::ranges::find(candidates[group.front()].callees, group.front()) !=
                                    candidates[group.front()].callees.end();
        if (!recursive) {
            pending.push_back(Unit{group, false});
            continue;
        }
        bool admitted = true;
        const std::size_t length = candidates[group.front()].function->contract->measures.size();
        for (const std::size_t member : group) {
            const vir::Function& function = *candidates[member].function;
            // Where the recursion is written: the first call into the group.
            source::SourceLocation at = function.range.begin;
            std::string partner;
            for (const vir::Expr* site : candidates[member].calls) {
                const auto found = position_of.find(std::get<vir::Call>(site->node).callee.usr);
                if (found == position_of.end()) {
                    continue; // defined in another unit, so not in this group
                }
                const std::size_t callee = found->second;
                if (std::ranges::find(group, callee) != group.end()) {
                    at = site->provenance.range.begin;
                    partner = std::get<vir::Call>(site->node).callee_name;
                    break;
                }
            }
            if (function.contract->measures.empty()) {
                report_termination(engine, function,
                                   group.size() == 1
                                       ? "it calls itself and states no measure"
                                       : "it calls '" + partner + "', which reaches it again, and it states no measure",
                                   at,
                                   "recursion is verified only when every function of it states 'decreases (...)' "
                                   "and every recursive call is made at a strictly smaller measure (SPEC.md "
                                   "TERMINATION-007)");
                admitted = false;
            } else if (function.contract->measures.size() != length) {
                const auto components = [](std::size_t count) {
                    return std::to_string(count) + (count == 1 ? " component" : " components");
                };
                report_termination(engine, function,
                                   "its measure has " + components(function.contract->measures.size()) +
                                       ", and that of '" + candidates[group.front()].function->qualified_name +
                                       "', which it recurses with, has " + components(length),
                                   function.contract->measure_range.begin,
                                   "the functions of one recursion share one ranking: each call within it compares "
                                   "the callee's measure with the caller's, component by component (SPEC.md "
                                   "TERMINATION-007)");
                admitted = false;
            }
        }
        if (admitted) {
            pending.push_back(Unit{group, true});
        }
    }

    DefinitionMap definitions = pure_definitions;
    std::map<std::string, std::size_t> established;

    // Contracts of other units, each established here only from an interface
    // that records the statement this unit builds from its own declaration
    // (SPEC.md TUBOUND-003, TUBOUND-004). Every one is checked, called or not: a
    // declaration this unit cannot establish is refused, as a declaration with
    // nothing to discharge it always was.
    for (const vir::Function* function : externals) {
        auto imported = import_contract(*function, pure_definitions, program, imports);
        if (!imported) {
            report(engine, imported.error());
            continue;
        }
        established.emplace(function->symbol.usr, program.contracts.size());
        program.contracts.push_back(std::move(*imported));
    }

    // Recursion across units is refused (SPEC.md TUBOUND-008).
    //
    // Every contract a proof of this unit relies on, transitively, is a node of
    // one dependency graph: each function of this unit, with an edge to each
    // function it calls, and each contract of another unit, with an edge to
    // each contract its record says its proof rests on, a function of this unit
    // or a record of a third. Recursion is verified only within one unit, where
    // measures are compared, so a cycle of that graph through a record of
    // another unit crosses a translation-unit boundary, and no contract whose
    // proof would rely on one is verified. The cycles are the graph's strongly
    // connected components with more than one member, or a member reaching
    // itself.
    std::vector<std::vector<std::size_t>> graph(candidates.size());
    std::vector<std::string> recorded;
    std::map<std::string, std::size_t> record_node;
    std::vector<std::string> unfilled;
    const auto node_of = [&](const std::string& symbol) -> std::size_t {
        if (const auto local = position_of.find(symbol); local != position_of.end()) {
            return local->second;
        }
        const auto [entry, added] = record_node.try_emplace(symbol, graph.size());
        if (added) {
            graph.emplace_back();
            recorded.push_back(symbol);
            unfilled.push_back(symbol);
        }
        return entry->second;
    };
    // What the record this unit uses for a contract says its proof rests on;
    // for a contract this unit does not declare, what an imported record says.
    const auto rests_on = [&](const std::string& symbol) -> const std::vector<artifact::Dependency>* {
        if (const auto found = established.find(symbol);
            found != established.end() && program.contracts[found->second].imported.has_value()) {
            return &program.contracts[found->second].imported->depends;
        }
        if (const ImportedEntry* entry = imports.find(symbol)) {
            return &entry->entry.depends;
        }
        return nullptr;
    };
    for (std::size_t position = 0; position < candidates.size(); ++position) {
        graph[position] = candidates[position].callees;
        for (const std::string& usr : candidates[position].external) {
            const std::size_t node = node_of(usr);
            graph[position].push_back(node);
        }
    }
    // Every record this unit established is a node, called or not, so one
    // resting on a cycle is withdrawn whether or not anything here uses it.
    for (const auto& [usr, index] : established) {
        node_of(usr);
    }
    while (!unfilled.empty()) {
        const std::string symbol = std::move(unfilled.back());
        unfilled.pop_back();
        const std::size_t node = record_node.at(symbol);
        if (const std::vector<artifact::Dependency>* depends = rests_on(symbol)) {
            for (const artifact::Dependency& dependency : *depends) {
                const std::size_t target = node_of(dependency.symbol);
                graph[node].push_back(target);
            }
        }
    }
    std::vector<bool> crossing(graph.size(), false);
    for (const std::vector<std::size_t>& component : recursion_groups(
             graph.size(), [&graph](std::size_t node) -> const std::vector<std::size_t>& { return graph[node]; })) {
        const bool through_record =
            std::ranges::any_of(component, [&candidates](std::size_t node) { return node >= candidates.size(); });
        const bool cyclic = component.size() > 1 || std::ranges::find(graph[component.front()], component.front()) !=
                                                        graph[component.front()].end();
        if (through_record && cyclic) {
            for (const std::size_t node : component) {
                crossing[node] = true;
            }
        }
    }
    // The first node on a cycle crossing units reached from `from`, if any.
    const auto reaches_crossing = [&graph, &crossing](std::size_t from) -> std::optional<std::size_t> {
        std::vector<bool> seen(graph.size(), false);
        std::vector<std::size_t> pending{from};
        seen[from] = true;
        while (!pending.empty()) {
            const std::size_t at = pending.back();
            pending.pop_back();
            if (crossing[at]) {
                return at;
            }
            for (const std::size_t next : graph[at]) {
                if (!seen[next]) {
                    seen[next] = true;
                    pending.push_back(next);
                }
            }
        }
        return std::nullopt;
    };
    const auto named = [&](std::size_t node) {
        return node < candidates.size() ? candidates[node].function->qualified_name
                                        : "the contract of another unit recorded for [" +
                                              artifact::displayed(recorded[node - candidates.size()]) + "]";
    };
    // A cycle through `start`, a node on one, as the callables it passes:
    // 'a' -> 'b' -> ... -> 'a'. A record's symbol is the interface's text.
    const auto cycle_through = [&](std::size_t start) {
        const auto symbol = [&](std::size_t node) {
            return "'" +
                   (node < candidates.size() ? candidates[node].function->symbol.usr
                                             : artifact::displayed(recorded[node - candidates.size()])) +
                   "'";
        };
        std::vector<std::size_t> previous(graph.size(), graph.size());
        std::vector<std::size_t> pending{start};
        std::optional<std::size_t> last;
        std::size_t next = 0;
        while (next < pending.size() && !last.has_value()) {
            const std::size_t at = pending[next++];
            for (const std::size_t target : graph[at]) {
                if (target == start) {
                    last = at;
                    break;
                }
                if (crossing[target] && previous[target] == graph.size()) {
                    previous[target] = at;
                    pending.push_back(target);
                }
            }
        }
        std::vector<std::size_t> path;
        for (std::size_t at = last.value_or(start); at != start; at = previous[at]) {
            path.push_back(at);
        }
        std::string text = symbol(start);
        for (const std::size_t node : std::ranges::reverse_view(path)) {
            text += " -> " + symbol(node);
        }
        return text + " -> " + symbol(start);
    };
    std::set<std::size_t> recursing;
    for (std::size_t position = 0; position < candidates.size(); ++position) {
        for (const std::string& usr : candidates[position].external) {
            const std::optional<std::size_t> cycle = reaches_crossing(node_of(usr));
            if (!cycle.has_value()) {
                continue;
            }
            const vir::Function& function = *candidates[position].function;
            const vir::Function& callee = *contracts.at(usr);
            source::SourceLocation at = function.range.begin;
            for (const vir::Expr* site : candidates[position].calls) {
                if (std::get<vir::Call>(site->node).callee.usr == usr) {
                    at = site->provenance.range.begin;
                    break;
                }
            }
            report(engine,
                   Refusal{"verified function '" + function.qualified_name + "' calls '" + callee.qualified_name +
                               "', whose contract another unit proved through a cycle of contracts that crosses "
                               "translation units, through " +
                               named(*cycle) + ": " + cycle_through(crossing[position] ? position : *cycle),
                           {"recursion across translation units is not verified: the measures that make "
                            "recursion sound are compared within one unit, and a contract whose proof relies on "
                            "a cycle through another unit's record is refused (SPEC.md TUBOUND-008, "
                            "TERMINATION-007)"},
                           at});
            recursing.insert(position);
            break;
        }
    }
    // A record on such a cycle, or resting on one, rests on a proof no unit
    // could have made first, so it is withdrawn with everything this unit would
    // rest on it (SPEC.md TUBOUND-008).
    std::set<std::string> withdrawn;
    for (const auto& [usr, index] : established) {
        const std::size_t node = record_node.at(usr);
        const std::optional<std::size_t> cycle = reaches_crossing(node);
        if (!cycle.has_value()) {
            continue;
        }
        report(engine, Refusal{"the contract of '" + contracts.at(usr)->qualified_name + "' that '" +
                                   artifact::displayed(program.contracts[index].imported->origin) +
                                   "' records rests on a cycle of verified contracts across units: " +
                                   cycle_through(crossing[node] ? node : *cycle),
                               {"recursion across translation units is not verified: a record resting on a "
                                "cycle through another unit's record is withdrawn, with every contract that "
                                "relies on it (SPEC.md TUBOUND-008)"},
                               contracts.at(usr)->range.begin});
        withdrawn.insert(usr);
    }
    if (!withdrawn.empty()) {
        std::erase_if(program.contracts, [&withdrawn](const ContractVerification& contract) {
            return contract.imported.has_value() && withdrawn.contains(contract.symbol);
        });
        std::erase_if(established, [&withdrawn](const auto& entry) { return withdrawn.contains(entry.first); });
        for (auto& [usr, index] : established) {
            const auto found = std::ranges::find_if(program.contracts, [&usr](const ContractVerification& contract) {
                return contract.imported.has_value() && contract.symbol == usr;
            });
            index = static_cast<std::size_t>(found - program.contracts.begin());
        }
    }

    const auto ready = [&](const Unit& unit) {
        return std::ranges::all_of(unit.members, [&](std::size_t member) {
            return !recursing.contains(member) &&
                   std::ranges::all_of(candidates[member].callees,
                                       [&](std::size_t callee) {
                                           return std::ranges::find(unit.members, callee) != unit.members.end() ||
                                                  established.contains(candidates[callee].function->symbol.usr);
                                       }) &&
                   std::ranges::all_of(candidates[member].external,
                                       [&](const std::string& usr) { return established.contains(usr); });
        });
    };
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto unit = pending.begin(); unit != pending.end();) {
            if (!ready(*unit)) {
                ++unit;
                continue;
            }
            if (unit->recursive) {
                std::vector<const vir::Function*> members;
                for (const std::size_t member : unit->members) {
                    members.push_back(candidates[member].function);
                }
                if (auto built = build_group(members, contracts, pure_definitions, established, program); !built) {
                    report(engine, *built.error().first, built.error().second, explain);
                }
                unit = pending.erase(unit);
                progress = true;
                continue;
            }
            const Candidate& candidate = candidates[unit->members.front()];
            const vir::Function& function = *candidate.function;
            if (candidate.unstated.has_value()) {
                report(engine, function, *candidate.unstated, explain);
                unit = pending.erase(unit);
                progress = true;
                continue;
            }
            // A loop, or a call whose contract is itself partial, leaves the
            // body without a total term; its contract is then partial too. So
            // does a call whose callee states memory capabilities: what such a
            // call owes is checked where the path makes it (SPEC.md 12.10
            // VERIFIED-043), which only the conditions walk does. So does an
            // operation C++ defines only under a condition: the condition is
            // owed where the path evaluates the operation, and the contract
            // rests on it (SPEC.md ARITH-009). So does a library operation,
            // which has no definition to unfold: only its summary is known,
            // supposed where the path makes it (RFC 0020 §6). So does a
            // validation, whose test is supposed the same way, and a verified
            // call a condition makes, whose postcondition is a fact of the
            // path it selects. So does a struct value assembled from its
            // members, which has no term (TRUST.md TCB-AGGREGATE-001).
            const bool partial = candidate.library || candidate.validates ||
                                 requires_conditions(*candidate.returned_value) ||
                                 contains_aggregate(*candidate.returned_value) ||
                                 calls_in_condition(*candidate.returned_value, contracts) ||
                                 first_definedness_site(*candidate.returned_value).has_value() ||
                                 std::ranges::any_of(candidate.calls, [&](const vir::Expr* call) {
                                     const std::string& callee = std::get<vir::Call>(call->node).callee.usr;
                                     const vir::Function& declared = *contracts.at(callee);
                                     return program.contracts[established.at(callee)].partial ||
                                            (declared.contract.has_value() && !declared.contract->capabilities.empty());
                                 });
            auto plan = partial ? build_partial(function, contracts, pure_definitions, established, program)
                                : build(function, contracts, pure_definitions, definitions, established, program);
            if (plan) {
                established.emplace(function.symbol.usr, program.contracts.size());
                program.contracts.push_back(std::move(*plan));
            } else {
                report(engine, function, plan.error(), explain);
            }
            unit = pending.erase(unit);
            progress = true;
        }
    }
    for (const Unit& unit : pending) {
        for (const std::size_t member : unit.members) {
            if (recursing.contains(member)) {
                continue; // refused above, where the recursion was found
            }
            const Candidate& candidate = candidates[member];
            std::string reason = "a verified callee is not available";
            source::SourceLocation location = candidate.function->range.begin;
            for (const vir::Expr* site : candidate.calls) {
                const auto& call = std::get<vir::Call>(site->node);
                if (!established.contains(call.callee.usr)) {
                    location = site->provenance.range.begin;
                    reason = "its callee '" + call.callee_name + "' has no established contract";
                    break;
                }
            }
            report(engine, *candidate.function, Failure{reason, location, {}}, explain);
        }
    }

    settle_totality(program, contracts, engine);
}

// The one statement of membership, shared with the quantifiers of laws, proofs
// and propositions, so a binder of a refinement type ranges over exactly the
// values a parameter of that type does (SPEC.md FORALL-001).
std::expected<std::optional<kernel::Proposition>, Failure> refinement_membership(const Program& program,
                                                                                 const vir::Type& type,
                                                                                 const kernel::Term& value) {
    return membership(program, type, value);
}

} // namespace cppl::obligations::detail
