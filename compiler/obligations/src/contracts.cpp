#include "lowering.hpp"

#include <algorithm>
#include <variant>

#include "cppl/kernel/substitution.hpp"

namespace cppl::obligations::detail {
namespace {

using Contracts = std::map<std::string, const vir::Function*>;

void report(diagnostics::Engine& engine, const vir::Function& function, const Failure& failure,
            const std::function<std::string(const Failure&)>& explain) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = diagnostics::Category::UnsupportedSemantics;
    diagnostic.location = failure.location.is_valid() ? failure.location : function.range.begin;
    diagnostic.message = "verified function '" + function.qualified_name +
                         "' cannot be stated to the formal core: " + failure.reason;
    const std::string note = explain(failure);
    if (!note.empty()) {
        diagnostic.notes.push_back({note, diagnostic.location});
    }
    engine.report(std::move(diagnostic));
}

kernel::Proposition quantify(const std::vector<kernel::Type>& parameters,
                            kernel::Proposition goal) {
    for (auto parameter = parameters.rbegin(); parameter != parameters.rend(); ++parameter) {
        goal = kernel::Proposition::for_all(*parameter, std::move(goal));
    }
    return goal;
}

kernel::Proposition specialize(kernel::Proposition proposition,
                              const std::vector<kernel::Type>& parameters,
                              const std::vector<kernel::Term>& arguments) {
    proposition = quantify(parameters, std::move(proposition));
    for (const auto& argument : arguments) {
        proposition = kernel::instantiate(*std::get<kernel::Forall>(proposition.node).body,
                                          argument);
    }
    return proposition;
}

kernel::Proposition postcondition_at(const ContractVerification& callee,
                                    std::vector<kernel::Term> arguments,
                                    kernel::Term value) {
    std::vector<kernel::Type> parameters = callee.parameters;
    parameters.push_back(callee.result);
    arguments.push_back(std::move(value));
    return specialize(callee.postcondition, parameters, arguments);
}

kernel::Proposition close(const ContractVerification& function, std::size_t prefix,
                         kernel::Proposition goal) {
    for (std::size_t index = prefix; index > 0; --index) {
        goal = kernel::Proposition::implication(
            kernel::shift(function.calls[index - 1].postcondition,
                          static_cast<std::uint32_t>(prefix - index)), std::move(goal));
    }
    if (function.precondition.has_value()) {
        goal = kernel::Proposition::implication(
            kernel::shift(*function.precondition, static_cast<std::uint32_t>(prefix)),
            std::move(goal));
    }
    for (std::size_t index = prefix; index > 0; --index) {
        goal = kernel::Proposition::for_all(function.calls[index - 1].result, std::move(goal));
    }
    return quantify(function.parameters, std::move(goal));
}

void collect_calls(const vir::Expr& expression, const Contracts& contracts,
                   std::vector<const vir::Expr*>& calls) {
    if (const auto* call = std::get_if<vir::Call>(&expression.node)) {
        for (const auto& argument : call->arguments) {
            collect_calls(argument, contracts, calls);
        }
        if (contracts.contains(call->callee.usr)) {
            calls.push_back(&expression);
        }
    } else if (const auto* binary = std::get_if<vir::Binary>(&expression.node)) {
        for (const auto& operand : binary->operands) {
            collect_calls(operand, contracts, calls);
        }
    }
}

Obligation obligation_for(const Program& program, const ContractVerification& function,
                          Origin origin, std::string subject, source::SourceRange range,
                          kernel::Proposition goal, const kernel::Proposition& reasoning) {
    Obligation obligation;
    obligation.origin = origin;
    obligation.subject = std::move(subject);
    obligation.range = std::move(range);
    obligation.goal = std::move(goal);
    obligation.id = identify_goal(program.context, obligation.subject, obligation.goal);
    if (!function.calls.empty()) {
        source::Hasher hasher;
        hasher.update_field("verified-call-composition-v1");
        hasher.update_field(obligation.id.digest.to_short_hex(64));
        hasher.update_field(identify_goal(program.context, obligation.subject, reasoning)
                                .digest.to_short_hex(64));
        for (const auto& call : function.calls) {
            const auto callee = std::ranges::find(program.contracts, call.callee,
                                                   &ContractVerification::function);
            hasher.update_field(program.obligations[callee->obligation].id.digest.to_short_hex(64));
        }
        obligation.id = ObligationId{hasher.finish()};
    }
    return obligation;
}

std::expected<ContractVerification, Failure> build(
    const vir::Function& function, const std::vector<const vir::Expr*>& sites,
    const DefinitionMap& pure_definitions, DefinitionMap& definitions,
    const std::map<std::string, std::size_t>& established, Program& program) {
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
        own_arguments.push_back(kernel::Term::variable(
            kernel::parameter_reference(plan.parameters.size(), index)));
    }
    plan.named_value = kernel::Term::call(definitions.at(function.symbol.usr), own_arguments);
    plan.theorem = close(plan, 0, kernel::instantiate(plan.postcondition, plan.named_value));

    CallBindings bindings;
    std::vector<Obligation> obligations;
    for (const auto* site : sites) {
        const auto& expression = std::get<vir::Call>(site->node);
        const auto& callee = program.contracts[established.at(expression.callee.usr)];
        if (expression.arguments.size() != callee.parameters.size()) {
            return std::unexpected(Failure{"call argument count differs from the contract",
                                            site->provenance.range.begin, {}});
        }
        const std::size_t prefix = plan.calls.size();
        CallVerification call;
        call.callee = callee.function;
        call.callee_name = expression.callee_name;
        call.result = callee.result;
        std::vector<kernel::Term> abstract_arguments;
        for (const auto& argument : expression.arguments) {
            auto actual = lower_value(argument, definitions, plan.parameters.size());
            auto abstract = lower_value(argument, pure_definitions, plan.parameters.size() + prefix,
                                        &bindings);
            if (!actual || !abstract) {
                return std::unexpected(!actual ? actual.error() : abstract.error());
            }
            call.arguments.push_back(std::move(*actual));
            abstract_arguments.push_back(std::move(*abstract));
        }
        call.value = kernel::Term::call(definitions.at(expression.callee.usr), call.arguments);
        if (callee.precondition.has_value()) {
            call.reasoning_goal = close(plan, prefix,
                specialize(*callee.precondition, callee.parameters, abstract_arguments));
            call.precondition_obligation = program.obligations.size() + obligations.size();
            obligations.push_back(obligation_for(
                program, plan, Origin::CallPrecondition,
                function.qualified_name + " -> " + expression.callee_name, site->provenance.range,
                close(plan, 0, specialize(*callee.precondition, callee.parameters, call.arguments)),
                *call.reasoning_goal));
        }
        for (auto& argument : abstract_arguments) {
            argument = kernel::shift(argument, 1);
        }
        call.postcondition = postcondition_at(callee, std::move(abstract_arguments),
                                               kernel::Term::variable(kernel::VarIndex{0}));
        plan.calls.push_back(std::move(call));
        bindings.emplace(site->id.value, plan.parameters.size() + prefix);
    }
    auto abstract_return = lower_value(*function.returned_value, pure_definitions,
                                       plan.parameters.size() + plan.calls.size(), &bindings);
    if (!abstract_return) {
        return std::unexpected(abstract_return.error());
    }
    const auto abstract_post = kernel::shift(plan.postcondition,
                                               static_cast<std::uint32_t>(plan.calls.size()), 1);
    plan.reasoning_goal = close(plan, plan.calls.size(),
                                  kernel::instantiate(abstract_post, *abstract_return));
    plan.obligation = program.obligations.size() + obligations.size();
    obligations.push_back(obligation_for(
        program, plan, Origin::FunctionContract, function.qualified_name, contract.range,
        close(plan, 0, kernel::instantiate(plan.postcondition, plan.returned_value)),
        plan.reasoning_goal));
    for (auto& obligation : obligations) {
        program.obligations.push_back(std::move(obligation));
    }
    return plan;
}

}  // namespace

void generate_contracts(const vir::Module& module, const DefinitionMap& pure_definitions,
                        Program& program, diagnostics::Engine& engine,
                        const std::function<std::string(const Failure&)>& explain) {
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
            auto plan = build(function, calls, pure_definitions, definitions, established, program);
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
                       function->range.begin, {}}, explain);
    }
}

}  // namespace cppl::obligations::detail
