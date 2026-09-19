#include "cppl/kernel/substitution.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <variant>

namespace cppl::obligations::detail {
namespace {

using Contracts = std::map<std::string, const vir::Function*>;

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
    for (auto parameter = parameters.rbegin(); parameter != parameters.rend(); ++parameter) {
        goal = kernel::Proposition::for_all(*parameter, std::move(goal));
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

kernel::Proposition postcondition_at(const ContractVerification& callee, std::vector<kernel::Term> arguments,
                                     kernel::Term value) {
    std::vector<kernel::Type> parameters = callee.parameters;
    parameters.push_back(callee.result);
    arguments.push_back(std::move(value));
    return specialize(callee.postcondition, parameters, arguments);
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
    if (function.precondition.has_value()) {
        goal = kernel::Proposition::implication(
            kernel::shift(*function.precondition, static_cast<std::uint32_t>(prefix)), std::move(goal));
    }
    for (std::size_t index = prefix; index > 0; --index) {
        goal = kernel::Proposition::for_all(path.calls[index - 1].result, std::move(goal));
    }
    return quantify(function.parameters, std::move(goal));
}

// The calls an expression evaluates, in evaluation order. A read of a local is
// not one of them: the call its value came from was evaluated where the local
// was written, and is collected there.
void collect_calls(const vir::Expr& expression, const Contracts& contracts, std::vector<const vir::Expr*>& calls) {
    if (const auto* call = std::get_if<vir::Call>(&expression.node)) {
        for (const auto& argument : call->arguments) {
            collect_calls(argument, contracts, calls);
        }
        if (contracts.contains(call->callee.usr)) {
            calls.push_back(&expression);
        }
    } else if (const auto* bound = std::get_if<vir::LocalVersion>(&expression.node)) {
        for (const auto& operand : bound->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* binary = std::get_if<vir::Binary>(&expression.node)) {
        for (const auto& operand : binary->operands) {
            collect_calls(operand, contracts, calls);
        }
    } else if (const auto* negation = std::get_if<vir::Negation>(&expression.node)) {
        for (const auto& operand : negation->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* branch = std::get_if<vir::Conditional>(&expression.node)) {
        for (const auto& operand : branch->operands)
            collect_calls(operand, contracts, calls);
    }
}

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

// One expression a path evaluates before it returns: a guard, whose outcome
// then holds on this path, or the value a local's version was given. Both are
// taken where the body evaluates them, so a call inside either is proven there
// and not where its value is eventually read.
struct Step {
    const vir::Expr* value;
    const vir::LocalVersion* binding; // null for a guard
    bool positive;                    // the guard's outcome on this path
};

struct Route {
    std::vector<Step> steps;
    const vir::Expr* returned;
};

bool routes(const vir::Expr& expression, std::vector<Step> steps, std::vector<Route>& result) {
    if (result.size() >= 128)
        return false;
    if (const auto* bound = std::get_if<vir::LocalVersion>(&expression.node)) {
        if (bound->operands.size() != 2)
            return false;
        steps.push_back(Step{&bound->operands[0], bound, true});
        return routes(bound->operands[1], std::move(steps), result);
    }
    if (const auto* branch = std::get_if<vir::Conditional>(&expression.node)) {
        if (branch->operands.size() != 3)
            return false;
        steps.push_back(Step{&branch->operands[0], nullptr, true});
        if (!routes(branch->operands[1], steps, result))
            return false;
        steps.back().positive = false;
        return routes(branch->operands[2], std::move(steps), result);
    }
    result.push_back(Route{std::move(steps), &expression});
    return true;
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
        if (callee.precondition.has_value()) {
            call.reasoning_goal = close(plan, path, prefix, call.conditions, true,
                                        specialize(*callee.precondition, callee.parameters, abstract_arguments));
            call.precondition_obligation = program.obligations.size() + obligations.size();
            obligations.push_back(
                obligation_for(program, path, Origin::CallPrecondition,
                               function.qualified_name + " -> " + call_expression.callee_name, site->provenance.range,
                               close(plan, path, 0, call.conditions, false,
                                     specialize(*callee.precondition, callee.parameters, call.arguments)),
                               *call.reasoning_goal));
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

std::expected<ContractVerification, Failure> build(const vir::Function& function, const Contracts& contracts,
                                                   const DefinitionMap& pure_definitions, DefinitionMap& definitions,
                                                   const std::map<std::string, std::size_t>& established,
                                                   Program& program) {
    ContractVerification plan;
    plan.function = function.id;
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
    auto post = lower_predicate(contract.postcondition, pure_definitions, plan.parameters.size() + 1);
    if (!post) {
        return std::unexpected(post.error());
    }
    plan.postcondition = std::move(*post);
    if (contract.precondition.has_value()) {
        auto pre = lower_predicate(*contract.precondition, pure_definitions, plan.parameters.size());
        if (!pre) {
            return std::unexpected(pre.error());
        }
        plan.precondition = std::move(*pre);
    }
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

} // namespace

void generate_contracts(const vir::Module& module, const DefinitionMap& pure_definitions, Program& program,
                        diagnostics::Engine& engine, const std::function<std::string(const Failure&)>& explain) {
    Contracts contracts;
    std::vector<const vir::Function*> pending;
    for (const auto& function : module.functions) {
        if (function.contract.has_value() && function.returned_value.has_value()) {
            contracts.emplace(function.symbol.usr, &function);
            pending.push_back(&function);
        }
    }
    DefinitionMap definitions = pure_definitions;
    std::map<std::string, std::size_t> established;
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto candidate = pending.begin(); candidate != pending.end();) {
            const auto& function = **candidate;
            std::vector<const vir::Expr*> calls;
            collect_calls(*function.returned_value, contracts, calls);
            if (!std::ranges::all_of(calls, [&](const vir::Expr* call) {
                    return established.contains(std::get<vir::Call>(call->node).callee.usr);
                })) {
                ++candidate;
                continue;
            }
            auto plan = build(function, contracts, pure_definitions, definitions, established, program);
            if (plan) {
                established.emplace(function.symbol.usr, program.contracts.size());
                program.contracts.push_back(std::move(*plan));
            } else {
                report(engine, function, plan.error(), explain);
            }
            candidate = pending.erase(candidate);
            progress = true;
        }
    }
    for (const auto* function : pending) {
        report(engine, *function,
               Failure{"a verified callee is not available: recursive or unsupported dependency",
                       function->range.begin,
                       {}},
               explain);
    }
}

} // namespace cppl::obligations::detail
