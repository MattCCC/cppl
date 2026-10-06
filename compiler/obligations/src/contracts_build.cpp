// Stating a function's contract: its obligations, the refinements its
// parameters and result carry, and the calls each path makes.

#include "contracts_conditions.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/storage.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/types.hpp"
#include "lowering.hpp"
#include "walks.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace cppl::obligations::detail {

using contracts::close;
using contracts::Conditions;
using contracts::specialize;

namespace contracts {

void report(diagnostics::Engine& engine, const vir::Function& function, const Failure& failure,
            const std::function<std::string(const Failure&)>& explain) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = diagnostics::Category::UnsupportedSemantics;
    diagnostic.location = failure.location.is_valid() ? failure.location : function.range.begin;
    diagnostic.message =
        "verified function '" + function.qualified_name + "' cannot be stated to the formal core: " + failure.reason;
    const std::string note = explain(failure);
    if (!note.empty()) {
        diagnostic.notes.push_back({note, diagnostic.location});
    }
    engine.report(std::move(diagnostic));
}

kernel::Proposition quantify(const std::vector<kernel::Type>& parameters, kernel::Proposition goal) {
    for (const auto& parameter : std::views::reverse(parameters)) {
        goal = kernel::Proposition::for_all(parameter, std::move(goal));
    }
    return goal;
}

kernel::Proposition specialize(kernel::Proposition proposition, const std::vector<kernel::Type>& parameters,
                               const std::vector<kernel::Term>& arguments) {
    proposition = quantify(parameters, std::move(proposition));
    for (const auto& argument : arguments) {
        proposition = kernel::instantiate(*std::get<kernel::Forall>(proposition.node).body, argument);
    }
    return proposition;
}

std::expected<std::optional<kernel::Proposition>, Failure> membership(const Program& program, const vir::Type& type,
                                                                      const kernel::Term& value) {
    std::optional<kernel::Proposition> required;
    const auto conjoin = [&required](kernel::Proposition next) {
        required = required.has_value() ? kernel::Proposition::conjunction(std::move(*required), std::move(next))
                                        : std::move(next);
    };
    if (type.is_value()) {
        const auto& projections = std::get<vir::ValueType>(type.node).projections;
        for (std::size_t index = 0; index < projections.size(); ++index) {
            const std::optional<kernel::Type> domain = core_type(type);
            const std::optional<kernel::Type> component = core_type(projections[index]);
            if (!domain || !domain->is_value() || !component) {
                continue;
            }
            auto inner = membership(program, projections[index],
                                    kernel::Term::project(*domain, static_cast<std::uint32_t>(index), value));
            if (!inner) {
                return inner;
            }
            if (inner->has_value()) {
                conjoin(std::move(**inner));
            }
        }
    }
    for (const vir::Refinement& refinement : type.refinements) {
        const RefinementPredicate* stated =
            program.refinement(refinement.identity.empty() ? refinement.name : refinement.identity);
        if (stated == nullptr || stated->parameters.empty()) {
            return std::unexpected(Failure{"refinement '" + refinement.name + "' has no resolved predicate", {}, {}});
        }
        // Indices first, then the value: the order the predicate binds them in.
        if (refinement.arguments.size() + 1 != stated->parameters.size()) {
            return std::unexpected(
                Failure{"refinement '" + refinement.name + "' has unresolved or mismatched indices", {}, {}});
        }
        std::vector<kernel::Term> arguments;
        arguments.reserve(stated->parameters.size());
        for (std::size_t index = 0; index < refinement.arguments.size(); ++index) {
            if (!stated->parameters[index].is_integer() ||
                !kernel::is_representable(stated->parameters[index].integer_type(), refinement.arguments[index])) {
                return std::unexpected(
                    Failure{"refinement '" + refinement.name + "' has an invalid index value", {}, {}});
            }
            arguments.push_back(
                kernel::Term::literal(stated->parameters[index].integer_type(), refinement.arguments[index]));
        }
        arguments.push_back(value);
        conjoin(specialize(stated->predicate, stated->parameters, arguments));
    }
    return required;
}

} // namespace contracts

namespace {

kernel::Proposition postcondition_at(const ContractVerification& callee, std::vector<kernel::Term> arguments,
                                     kernel::Term value) {
    std::vector<kernel::Type> parameters = callee.parameters;
    parameters.push_back(callee.result);
    arguments.push_back(std::move(value));
    return specialize(callee.postcondition, parameters, arguments);
}

} // namespace

namespace contracts {

// What a normal return of `function` is charged at its post-state `arguments`
// and result `value` (SPEC.md REFINE-062, CLASS-010).
kernel::Proposition owed_at_return(const ContractVerification& function, std::vector<kernel::Term> arguments,
                                   kernel::Term value) {
    std::vector<kernel::Type> parameters = function.parameters;
    parameters.push_back(function.result);
    arguments.push_back(std::move(value));
    return specialize(function.owed_at_return, parameters, arguments);
}

kernel::Proposition close(const ContractVerification& function, const ReturnPath& path, std::size_t prefix,
                          std::size_t conditions, bool abstract, kernel::Proposition goal) {
    for (std::size_t index = prefix; index > 0; --index) {
        goal = kernel::Proposition::implication(
            kernel::shift(path.calls[index - 1].postcondition, static_cast<std::uint32_t>(prefix - index)),
            std::move(goal));
    }
    for (std::size_t index = conditions; index > 0; --index) {
        const auto& condition = path.conditions[index - 1];
        goal = kernel::Proposition::implication(
            abstract ? kernel::shift(condition.abstract, static_cast<std::uint32_t>(prefix - condition.calls))
                     : condition.actual,
            std::move(goal));
    }
    for (const auto& precondition : std::views::reverse(function.preconditions)) {
        goal = kernel::Proposition::implication(kernel::shift(precondition, static_cast<std::uint32_t>(prefix)),
                                                std::move(goal));
    }
    for (std::size_t index = prefix; index > 0; --index) {
        goal = kernel::Proposition::for_all(path.calls[index - 1].result, std::move(goal));
    }
    return quantify(function.parameters, std::move(goal));
}

} // namespace contracts

namespace {

Obligation obligation_for(const Program& program, const ReturnPath& path, Origin origin, std::string subject,
                          source::SourceRange range, kernel::Proposition goal, const kernel::Proposition& reasoning) {
    Obligation obligation;
    obligation.origin = origin;
    obligation.subject = std::move(subject);
    obligation.range = std::move(range);
    obligation.goal = std::move(goal);
    obligation.id = identify_goal(program.context, obligation.subject, obligation.goal);
    if (!path.calls.empty()) {
        source::Hasher hasher;
        hasher.update_field("verified-call-composition-v1");
        hasher.update_field(obligation.id.digest.to_short_hex(64));
        hasher.update_field(identify_goal(program.context, obligation.subject, reasoning).digest.to_short_hex(64));
        for (const auto& call : path.calls) {
            const auto callee = std::ranges::find(program.contracts, call.callee, &ContractVerification::function);
            hasher.update_field(program.obligations[callee->obligation].id.digest.to_short_hex(64));
        }
        obligation.id = ObligationId{hasher.finish()};
    }
    return obligation;
}

std::expected<void, Failure> append_calls(const vir::Expr& expression, const vir::Function& function,
                                          const Contracts& contracts, const ContractVerification& plan,
                                          ReturnPath& path, CallBindings& bindings, const VersionBindings& versions,
                                          const DefinitionMap& pure_definitions, const DefinitionMap& definitions,
                                          const std::map<std::string, std::size_t>& established, const Program& program,
                                          std::vector<Obligation>& obligations) {
    std::vector<const vir::Expr*> sites;
    collect_calls(expression, contracts, sites);
    for (const auto* site : sites) {
        if (bindings.contains(site->id.value)) {
            continue;
        }
        const auto& call_expression = std::get<vir::Call>(site->node);
        const auto& callee = program.contracts[established.at(call_expression.callee.usr)];
        if (call_expression.arguments.size() != callee.parameters.size()) {
            return std::unexpected(
                Failure{"call argument count differs from the contract", site->provenance.range.begin, {}});
        }
        const std::size_t prefix = path.calls.size();
        CallVerification call;
        call.callee = callee.function;
        call.callee_name = call_expression.callee_name;
        call.result = callee.result;
        call.conditions = path.conditions.size();
        std::vector<kernel::Term> abstract_arguments;
        for (const auto& argument : call_expression.arguments) {
            auto actual = lower_value(argument, definitions, plan.parameters.size(), nullptr, &versions);
            auto abstract =
                lower_value(argument, pure_definitions, plan.parameters.size() + prefix, &bindings, &versions);
            if (!actual || !abstract) {
                return std::unexpected(!actual ? actual.error() : abstract.error());
            }
            call.arguments.push_back(std::move(*actual));
            abstract_arguments.push_back(std::move(*abstract));
        }
        call.value = kernel::Term::call(definitions.at(call_expression.callee.usr), call.arguments);
        for (const auto& precondition : callee.preconditions) {
            CallPrecondition required;
            required.reasoning_goal = close(plan, path, prefix, call.conditions, true,
                                            specialize(precondition, callee.parameters, abstract_arguments));
            required.obligation = program.obligations.size() + obligations.size();
            obligations.push_back(obligation_for(program, path, Origin::CallPrecondition,
                                                 function.qualified_name + " -> " + call_expression.callee_name,
                                                 site->provenance.range,
                                                 close(plan, path, 0, call.conditions, false,
                                                       specialize(precondition, callee.parameters, call.arguments)),
                                                 required.reasoning_goal));
            call.preconditions.push_back(std::move(required));
        }
        for (auto& argument : abstract_arguments)
            argument = kernel::shift(argument, 1);
        call.postcondition =
            postcondition_at(callee, std::move(abstract_arguments), kernel::Term::variable(kernel::VarIndex{0}));
        path.calls.push_back(std::move(call));
        bindings.emplace(site->id.value, plan.parameters.size() + prefix);
    }
    return {};
}

} // namespace

namespace contracts {

// The contract itself: its types, postcondition and precondition, lowered
// from the specification expressions alone.
std::expected<void, Failure> state_contract(const vir::Function& function, const DefinitionMap& pure_definitions,
                                            const Program& program, ContractVerification& plan) {
    plan.function = function.id;
    plan.name = function.qualified_name;
    plan.symbol = function.symbol.usr;
    plan.library_models = function.library_models;
    if (!function.contract.has_value()) {
        return std::unexpected(Failure{"the function states no contract", function.range.begin, {}});
    }
    const auto result = core_type(function.result);
    if (!result.has_value()) {
        return std::unexpected(Failure{"the result type is not modeled", function.range.begin, {}});
    }
    plan.result = *result;
    for (const auto& parameter : function.parameters) {
        const auto type = core_type(parameter.type);
        if (!type.has_value()) {
            return std::unexpected(Failure{"a parameter type is not modeled", function.range.begin, {}});
        }
        plan.parameters.push_back(*type);
    }
    const auto& contract = *function.contract;
    auto post = lower_predicate(contract.postcondition, program, pure_definitions, plan.parameters.size() + 1);
    if (!post) {
        return std::unexpected(post.error());
    }
    plan.postcondition = std::move(*post);

    // A refined result is part of what the function guarantees, so it is stated
    // with the postcondition and proven on every path that returns (SPEC.md
    // 17.2). The value is the innermost variable here, as `result` is.
    const auto result_membership = membership(program, function.result, kernel::Term::variable(kernel::VarIndex{0}));
    if (!result_membership) {
        return std::unexpected(result_membership.error());
    }
    if (result_membership->has_value()) {
        plan.postcondition = kernel::Proposition::conjunction(std::move(plan.postcondition), **result_membership);
    }

    for (const auto& precondition : contract.preconditions) {
        auto pre = lower_predicate(precondition, program, pure_definitions, plan.parameters.size());
        if (!pre) {
            return std::unexpected(pre.error());
        }
        plan.preconditions.push_back(std::move(*pre));
    }

    // A refined parameter is known to satisfy its predicate inside the body: the
    // caller proved it where the value entered the type, so here it is supposed
    // rather than proven again (SPEC.md 17.3). The author does not restate it.
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const auto refined =
            membership(program, function.parameters[index].type,
                       kernel::Term::variable(kernel::parameter_reference(plan.parameters.size(), index)));
        if (!refined) {
            return std::unexpected(refined.error());
        }
        if (refined->has_value()) {
            plan.preconditions.push_back(**refined);
        }
    }
    plan.owed_at_return = plan.postcondition;
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (!source::aliases_storage(function.parameters[index].passing))
            continue;
        auto required =
            membership(program, function.parameters[index].type,
                       kernel::Term::variable(kernel::parameter_reference(plan.parameters.size() + 1, index)));
        if (!required)
            return std::unexpected(required.error());
        if (*required)
            plan.postcondition = kernel::Proposition::conjunction(std::move(plan.postcondition), **required);
    }
    // A measure is a function of the parameters alone, defined for every
    // argument, and each component ranges over a well-founded domain (SPEC.md
    // TERMINATION-005, 22.5).
    for (const vir::Expr& measure : contract.measures) {
        if (auto domain = measure_domain(measure, "a function measure"); !domain) {
            return std::unexpected(domain.error());
        }
        if (auto lowered = lower_value(measure, pure_definitions, plan.parameters.size()); !lowered) {
            return std::unexpected(lowered.error());
        }
    }
    // What the contract states, identified the same way in every unit, so an
    // interface can record it and another unit's declaration be compared with
    // it (SPEC.md TUBOUND-004). Only a function with external linkage is one entity
    // across units; any other is a different function in each, however alike
    // its identity is spelled. A statement that cannot be identified is not an
    // error here: the contract is still verified, and simply cannot cross.
    if (function.external_linkage) {
        if (auto statement = state_statement(function, plan, program.context, pure_definitions)) {
            plan.statement = statement->identity;
            plan.description = std::move(statement->description);
        }
        if (auto crossing = state_statement(function, plan, program.context, pure_definitions, Measures::Requested)) {
            plan.interface_statement = crossing->identity;
        }
    }
    return {};
}

std::expected<ContractVerification, Failure> build(const vir::Function& function, const Contracts& contracts,
                                                   const DefinitionMap& pure_definitions, DefinitionMap& definitions,
                                                   const std::map<std::string, std::size_t>& established,
                                                   Program& program) {
    ContractVerification plan;
    if (auto stated = state_contract(function, pure_definitions, program, plan); !stated) {
        return std::unexpected(stated.error());
    }
    if (!function.contract.has_value()) {
        return std::unexpected(Failure{"the function states no contract", function.range.begin, {}});
    }
    if (!function.returned_value.has_value()) {
        return std::unexpected(Failure{"the function has no return tree", function.range.begin, {}});
    }
    const auto& contract = *function.contract;
    auto returned = lower_value(*function.returned_value, definitions, plan.parameters.size());
    if (!returned) {
        return std::unexpected(returned.error());
    }
    plan.returned_value = std::move(*returned);

    if (!definitions.contains(function.symbol.usr)) {
        kernel::Definition definition;
        definition.id = kernel::DefId{static_cast<std::uint32_t>(program.context.definition_count())};
        definition.name = function.qualified_name;
        definition.parameters = plan.parameters;
        definition.result = plan.result;
        definition.body = plan.returned_value;
        const auto id = definition.id;
        if (auto admitted = program.context.define(std::move(definition)); !admitted) {
            return std::unexpected(Failure{admitted.error().detail, function.range.begin, {}});
        }
        definitions.emplace(function.symbol.usr, id);
    }
    std::vector<kernel::Term> own_arguments;
    own_arguments.reserve(plan.parameters.size());
    for (std::size_t index = 0; index < plan.parameters.size(); ++index) {
        own_arguments.push_back(kernel::Term::variable(kernel::parameter_reference(plan.parameters.size(), index)));
    }
    plan.named_value = kernel::Term::call(definitions.at(function.symbol.usr), own_arguments);
    plan.theorem = close(plan, {}, 0, 0, false, kernel::instantiate(plan.postcondition, plan.named_value));

    std::vector<Route> leaves;
    if (!routes(*function.returned_value, {}, leaves)) {
        return std::unexpected(
            Failure{"malformed return tree or more than 128 return paths", function.range.begin, {}});
    }
    std::vector<Obligation> obligations;
    for (const auto& leaf : leaves) {
        ReturnPath path;
        CallBindings bindings;
        VersionBindings versions;
        for (const auto& step : leaf.steps) {
            auto calls = append_calls(*step.value, function, contracts, plan, path, bindings, versions,
                                      pure_definitions, definitions, established, program, obligations);
            if (!calls)
                return std::unexpected(calls.error());
            if (step.binding != nullptr) {
                // A value entering a refinement type must be shown to satisfy its
                // predicate, where it enters it and under what the path supposes
                // there (SPEC.md 17.2). Nothing is assumed from the declaration.
                if (step.binding->declared.is_refined()) {
                    auto actual = lower_value(*step.value, definitions, plan.parameters.size(), nullptr, &versions);
                    auto abstract = lower_value(*step.value, pure_definitions,
                                                plan.parameters.size() + path.calls.size(), &bindings, &versions);
                    if (!actual || !abstract) {
                        return std::unexpected(!actual ? actual.error() : abstract.error());
                    }
                    const auto required = membership(program, step.binding->declared, *actual);
                    const auto reasoning = membership(program, step.binding->declared, *abstract);
                    if (!required || !reasoning) {
                        return std::unexpected(!required ? required.error() : reasoning.error());
                    }
                    if (required->has_value() && reasoning->has_value()) {
                        obligations.push_back(obligation_for(
                            program, path, Origin::RefinementIntroduction,
                            function.qualified_name + " -> " + refinement_label(step.binding->declared),
                            step.value->provenance.range,
                            close(plan, path, 0, path.conditions.size(), false, **required),
                            close(plan, path, path.calls.size(), path.conditions.size(), true, **reasoning)));
                    }
                }
                if (!versions.emplace(step.binding->version, step.value).second) {
                    return std::unexpected(Failure{"malformed local version", step.value->provenance.range.begin, {}});
                }
                continue;
            }
            auto actual = lower_value(*step.value, definitions, plan.parameters.size(), nullptr, &versions);
            auto abstract = lower_value(*step.value, pure_definitions, plan.parameters.size() + path.calls.size(),
                                        &bindings, &versions);
            if (!actual || !abstract)
                return std::unexpected(!actual ? actual.error() : abstract.error());
            path.conditions.push_back(PathCondition{kernel::predicate(*actual, step.positive),
                                                    kernel::predicate(*abstract, step.positive), path.calls.size()});
        }
        auto calls = append_calls(*leaf.returned, function, contracts, plan, path, bindings, versions, pure_definitions,
                                  definitions, established, program, obligations);
        if (!calls)
            return std::unexpected(calls.error());
        auto actual_return = lower_value(*leaf.returned, definitions, plan.parameters.size(), nullptr, &versions);
        auto abstract_return = lower_value(*leaf.returned, pure_definitions, plan.parameters.size() + path.calls.size(),
                                           &bindings, &versions);
        if (!actual_return || !abstract_return) {
            return std::unexpected(!actual_return ? actual_return.error() : abstract_return.error());
        }
        path.returned_value = std::move(*actual_return);
        const auto abstract_post = kernel::shift(plan.postcondition, static_cast<std::uint32_t>(path.calls.size()), 1);
        path.reasoning_goal = close(plan, path, path.calls.size(), path.conditions.size(), true,
                                    kernel::instantiate(abstract_post, *abstract_return));
        path.obligation = program.obligations.size() + obligations.size();
        obligations.push_back(obligation_for(
            program, path, leaves.size() == 1 ? Origin::FunctionContract : Origin::ReturnPath,
            function.qualified_name + (leaves.size() == 1 ? "" : " path " + std::to_string(plan.paths.size() + 1)),
            leaf.returned->provenance.range,
            close(plan, path, 0, path.conditions.size(), false,
                  kernel::instantiate(plan.postcondition, path.returned_value)),
            path.reasoning_goal));
        plan.paths.push_back(std::move(path));
    }
    plan.obligation = plan.paths.front().obligation;
    if (leaves.size() > 1) {
        plan.obligation = program.obligations.size() + obligations.size();
        auto goal = close(plan, {}, 0, 0, false, kernel::instantiate(plan.postcondition, plan.returned_value));
        auto obligation =
            obligation_for(program, {}, Origin::FunctionContract, function.qualified_name, contract.range, goal, goal);
        source::Hasher hasher;
        hasher.update_field("verified-paths-v1");
        hasher.update_field(obligation.id.digest.to_short_hex(64));
        for (const auto& dependency : obligations)
            hasher.update_field(dependency.id.digest.to_short_hex(64));
        obligation.id = ObligationId{hasher.finish()};
        obligations.push_back(std::move(obligation));
    }
    for (auto& obligation : obligations) {
        program.obligations.push_back(std::move(obligation));
    }
    return plan;
}

} // namespace contracts

namespace {

// The conditions of a contract already stated in `plan`, pushed into the
// program. Returns the contract's content identity, which a recursion group
// assigns only once every member is built, since until then its members' calls
// are identified by what their contracts state.
std::expected<source::Digest, Failure> fill_conditions(const vir::Function& function, ContractVerification& plan,
                                                       const Contracts& contracts,
                                                       const DefinitionMap& pure_definitions,
                                                       const std::map<std::string, std::size_t>& established,
                                                       Program& program) {
    Conditions generated(function, plan, contracts, pure_definitions, established, program, plan.recursion);
    if (auto run = generated.run(); !run) {
        return std::unexpected(run.error());
    }
    if (generated.obligations.empty()) {
        return std::unexpected(Failure{"the body produced no obligation", function.range.begin, {}});
    }
    source::Hasher hasher;
    hasher.update_field("partial-contract-v1");
    hasher.update_field(function.qualified_name);
    for (const Obligation& obligation : generated.obligations) {
        hasher.update_field(obligation.id.digest.to_short_hex(64));
    }
    plan.conditions = std::move(generated.conditions);
    plan.unsafe_regions = std::move(generated.unsafe_regions);
    plan.unmeasured_loops = std::move(generated.unmeasured_loops);
    plan.validations = std::move(generated.validations);
    for (Obligation& obligation : generated.obligations) {
        program.obligations.push_back(std::move(obligation));
    }
    std::ranges::move(generated.claims, std::back_inserter(program.path_claims));
    return hasher.finish();
}

} // namespace

namespace contracts {

std::expected<ContractVerification, Failure> build_partial(const vir::Function& function, const Contracts& contracts,
                                                           const DefinitionMap& pure_definitions,
                                                           const std::map<std::string, std::size_t>& established,
                                                           Program& program) {
    ContractVerification plan;
    if (auto stated = state_contract(function, pure_definitions, program, plan); !stated) {
        return std::unexpected(stated.error());
    }
    plan.partial = true;
    auto identity = fill_conditions(function, plan, contracts, pure_definitions, established, program);
    if (!identity) {
        return std::unexpected(identity.error());
    }
    plan.identity = *identity;
    return plan;
}

} // namespace contracts

namespace {

// What a recursive contract states, identified before its body is: its
// preconditions, its postcondition and its measure. A call within the group
// rests on exactly that, the induction hypothesis, so it is what such a call's
// obligation is identified by.
source::Digest statement_identity(const vir::Function& function, const ContractVerification& plan) {
    source::Hasher hasher;
    hasher.update_field("recursive-contract-statement-v1");
    hasher.update_field(function.qualified_name);
    for (const kernel::Proposition& precondition : plan.preconditions) {
        hasher.update_field(kernel::describe(precondition));
    }
    hasher.update_field(kernel::describe(plan.postcondition));
    if (function.contract.has_value()) {
        for (const vir::Expr& measure : function.contract->measures) {
            hasher.update_field(vir::describe(measure));
        }
    }
    return hasher.finish();
}

} // namespace

namespace contracts {

// A recursion group: functions that call each other, verified together
// (SPEC.md TERMINATION-007). Every member's contract is stated and reserved
// first, so a call within the group finds the contract it supposes; then each
// body's conditions are generated, a call within the group owing a smaller
// measure. If any member cannot be stated or built, none is: a member's proof
// supposes the others', so what was generated for the group is withdrawn.
std::expected<void, std::pair<const vir::Function*, Failure>> build_group(
    const std::vector<const vir::Function*>& members, const Contracts& contracts, const DefinitionMap& pure_definitions,
    std::map<std::string, std::size_t>& established, Program& program) {
    const std::size_t first_contract = program.contracts.size();
    const std::size_t first_obligation = program.obligations.size();
    const std::size_t first_claim = program.path_claims.size();
    const auto withdraw = [&] {
        program.contracts.erase(program.contracts.begin() + static_cast<std::ptrdiff_t>(first_contract),
                                program.contracts.end());
        program.obligations.erase(program.obligations.begin() + static_cast<std::ptrdiff_t>(first_obligation),
                                  program.obligations.end());
        program.path_claims.erase(program.path_claims.begin() + static_cast<std::ptrdiff_t>(first_claim),
                                  program.path_claims.end());
        for (const vir::Function* member : members) {
            established.erase(member->symbol.usr);
        }
    };
    std::vector<std::size_t> group;
    for (const vir::Function* member : members) {
        ContractVerification plan;
        if (auto stated = state_contract(*member, pure_definitions, program, plan); !stated) {
            withdraw();
            return std::unexpected(std::pair{member, stated.error()});
        }
        plan.partial = true;
        plan.identity = statement_identity(*member, plan);
        group.push_back(program.contracts.size());
        established.emplace(member->symbol.usr, program.contracts.size());
        program.contracts.push_back(std::move(plan));
    }
    for (const std::size_t index : group) {
        program.contracts[index].recursion = group;
    }
    std::vector<source::Digest> identities;
    for (std::size_t position = 0; position < members.size(); ++position) {
        auto identity = fill_conditions(*members[position], program.contracts[group[position]], contracts,
                                        pure_definitions, established, program);
        if (!identity) {
            withdraw();
            return std::unexpected(std::pair{members[position], identity.error()});
        }
        identities.push_back(*identity);
    }
    for (std::size_t position = 0; position < members.size(); ++position) {
        program.contracts[group[position]].identity = identities[position];
    }
    return {};
}

} // namespace contracts

} // namespace cppl::obligations::detail
