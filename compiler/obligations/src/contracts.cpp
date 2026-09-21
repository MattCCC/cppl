#include "cppl/kernel/substitution.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <ranges>
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

// What a value must satisfy to stand as a value of `type` (SPEC.md 17.2).
//
// A refinement of a refinement states both predicates, because both apply to the
// value (SPEC.md 17.5), and an indexed refinement states its predicate at the
// values its indices were applied at (SPEC.md 18). An unrefined type requires
// nothing, which is what makes ordinary C++ unaffected.
std::expected<std::optional<kernel::Proposition>, Failure> membership(const Program& program, const vir::Type& type,
                                                                      const kernel::Term& value) {
    std::optional<kernel::Proposition> required;
    for (const vir::Refinement& refinement : type.refinements) {
        const RefinementPredicate* stated = program.refinement(refinement.name);
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
        kernel::Proposition applied = specialize(stated->predicate, stated->parameters, arguments);
        required = required.has_value() ? kernel::Proposition::conjunction(std::move(*required), std::move(applied))
                                        : std::move(applied);
    }
    return required;
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
    for (const auto& precondition : std::views::reverse(function.preconditions)) {
        goal = kernel::Proposition::implication(kernel::shift(precondition, static_cast<std::uint32_t>(prefix)),
                                                std::move(goal));
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
    } else if (const auto* loop = std::get_if<vir::Loop>(&expression.node)) {
        for (const auto& operand : loop->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* next = std::get_if<vir::Iterate>(&expression.node)) {
        for (const auto& operand : next->operands)
            collect_calls(operand, contracts, calls);
    }
}

bool contains_loop(const vir::Expr& expression) {
    if (std::holds_alternative<vir::Loop>(expression.node)) {
        return true;
    }
    if (const auto* bound = std::get_if<vir::LocalVersion>(&expression.node)) {
        return std::ranges::any_of(bound->operands, contains_loop);
    }
    if (const auto* branch = std::get_if<vir::Conditional>(&expression.node)) {
        return std::ranges::any_of(branch->operands, contains_loop);
    }
    return false;
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

// The contract itself: its types, postcondition and precondition, lowered
// from the specification expressions alone.
std::expected<void, Failure> state_contract(const vir::Function& function, const DefinitionMap& pure_definitions,
                                            const Program& program, ContractVerification& plan) {
    plan.function = function.id;
    plan.name = function.qualified_name;
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
    auto post = lower_predicate(contract.postcondition, pure_definitions, plan.parameters.size() + 1);
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
        auto pre = lower_predicate(precondition, pure_definitions, plan.parameters.size());
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
                            function.qualified_name + " -> " + step.binding->declared.refinements.front().name,
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

// Bounds the nodes one body's conditions are generated from. The bridge
// already bounds paths and statements; this keeps malformed VIR finite too.
constexpr std::size_t kMaxConditionSteps = std::size_t{1} << 15;

// Partial correctness (SPEC.md 23, 24).
//
// A body with a loop is not one total core term, so its contract cannot be a
// theorem about a definition. It is established from verification conditions,
// each an ordinary proposition the kernel decides: on every path, what the path
// supposes implies what must hold where the path stands. A value the path
// learns only through a proposition - a verified call's result, or a carried
// local at a loop head - is a fresh variable bound where the path meets it,
// followed by the proposition supposed of it. Which conditions a body needs is
// decided here, by the rules for calls and loops; that is a correspondence
// responsibility (TRUST.md 41.2). Whether each holds is the kernel's.
class Conditions {
  public:
    Conditions(const vir::Function& function, const ContractVerification& plan, const Contracts& contracts,
               const DefinitionMap& definitions, const std::map<std::string, std::size_t>& established,
               const Program& program)
        : function_(function),
          plan_(plan),
          contracts_(contracts),
          definitions_(definitions),
          established_(established),
          program_(program) {}

    std::expected<void, Failure> run() {
        if (!function_.returned_value.has_value()) {
            return fail("the function has no return tree", function_.range.begin);
        }
        Scope scope;
        scope.binders = plan_.parameters;
        for (const auto& precondition : plan_.preconditions) {
            scope.events.emplace_back(precondition);
        }
        std::vector<Active> loops;
        return walk(*function_.returned_value, std::move(scope), loops);
    }

    std::vector<Obligation> obligations;
    std::vector<VerificationCondition> conditions;

  private:
    // After the parameters, a path binds fresh values and supposes facts, in
    // the order it meets them. A step is one or the other, never both, so the
    // alternative carries that exclusivity instead of a pair of optionals.
    using Event = std::variant<kernel::Type, kernel::Proposition>;

    struct Scope {
        std::vector<kernel::Type> binders; // the parameters, then each fresh value
        std::vector<Event> events;
        CallBindings calls;
        VersionBindings versions;
        OpaqueBindings opaque;
        std::vector<std::size_t> relied_on; // contracts whose postconditions are supposed
    };

    struct Active {
        const vir::Loop* loop;
        std::vector<kernel::Type> carried;
    };

    static std::unexpected<Failure> fail(std::string reason, const source::SourceLocation& location) {
        return std::unexpected(Failure{std::move(reason), location, {}});
    }

    [[nodiscard]] kernel::Proposition close(const Scope& scope, kernel::Proposition goal) const {
        for (const auto& event : std::ranges::reverse_view(scope.events)) {
            goal = std::holds_alternative<kernel::Type>(event)
                       ? kernel::Proposition::for_all(std::get<kernel::Type>(event), std::move(goal))
                       : kernel::Proposition::implication(std::get<kernel::Proposition>(event), std::move(goal));
        }
        return quantify(plan_.parameters, std::move(goal));
    }

    [[nodiscard]] std::expected<kernel::Term, Failure> lower(const vir::Expr& expression, const Scope& scope) const {
        return lower_value(expression, definitions_, scope.binders.size(), &scope.calls, &scope.versions,
                           &scope.opaque);
    }

    [[nodiscard]] std::string identity_of(std::size_t index) const {
        const ContractVerification& callee = program_.contracts[index];
        return callee.partial ? callee.identity.to_short_hex(64)
                              : program_.obligations[callee.obligation].id.digest.to_short_hex(64);
    }

    void emit(const Scope& scope, Origin origin, std::string subject, const source::SourceRange& range,
              kernel::Proposition goal) {
        Obligation obligation;
        obligation.origin = origin;
        obligation.subject = std::move(subject);
        obligation.range = range;
        obligation.goal = close(scope, std::move(goal));
        source::Hasher hasher;
        hasher.update_field("partial-correctness-v1");
        hasher.update_field(
            identify_goal(program_.context, obligation.subject, obligation.goal).digest.to_short_hex(64));
        for (const std::size_t callee : scope.relied_on) {
            hasher.update_field(identity_of(callee));
        }
        obligation.id = ObligationId{hasher.finish()};
        conditions.push_back(VerificationCondition{program_.obligations.size() + obligations.size(), scope.relied_on});
        obligations.push_back(std::move(obligation));
    }

    // Each verified call the expression evaluates, where it evaluates it: its
    // precondition is a condition under what the path supposes so far, and its
    // result is a fresh value of which the callee's postcondition is supposed.
    std::expected<void, Failure> evaluate(const vir::Expr& expression, Scope& scope) {
        std::vector<const vir::Expr*> sites;
        collect_calls(expression, contracts_, sites);
        for (const vir::Expr* site : sites) {
            if (scope.calls.contains(site->id.value)) {
                continue;
            }
            const auto& call = std::get<vir::Call>(site->node);
            const auto found = established_.find(call.callee.usr);
            if (found == established_.end()) {
                return fail("'" + call.callee_name + "' has no established contract", site->provenance.range.begin);
            }
            const ContractVerification& callee = program_.contracts[found->second];
            if (call.arguments.size() != callee.parameters.size()) {
                return fail("call argument count differs from the contract", site->provenance.range.begin);
            }
            std::vector<kernel::Term> arguments;
            for (const vir::Expr& argument : call.arguments) {
                auto lowered = lower(argument, scope);
                if (!lowered) {
                    return std::unexpected(lowered.error());
                }
                arguments.push_back(std::move(*lowered));
            }
            for (const auto& precondition : callee.preconditions) {
                emit(scope, Origin::CallPrecondition, function_.qualified_name + " -> " + call.callee_name,
                     site->provenance.range, specialize(precondition, callee.parameters, arguments));
            }
            for (auto& argument : arguments) {
                argument = kernel::shift(argument, 1);
            }
            scope.calls.emplace(site->id.value, scope.binders.size());
            scope.binders.push_back(callee.result);
            scope.events.emplace_back(callee.result);
            scope.events.emplace_back(
                postcondition_at(callee, std::move(arguments), kernel::Term::variable(kernel::VarIndex{0})));
            if (std::ranges::find(scope.relied_on, found->second) == scope.relied_on.end()) {
                scope.relied_on.push_back(found->second);
            }
        }
        return {};
    }

    std::expected<void, Failure> walk(const vir::Expr& expression, Scope scope, std::vector<Active>& loops) {
        const source::SourceLocation& location = expression.provenance.range.begin;
        if (++steps_ > kMaxConditionSteps) {
            return fail("this body has more than " + std::to_string(kMaxConditionSteps) + " modeled steps", location);
        }

        if (const auto* bound = std::get_if<vir::LocalVersion>(&expression.node)) {
            if (bound->operands.size() != 2 || scope.versions.contains(bound->version) ||
                scope.opaque.contains(bound->version)) {
                return fail("malformed local version", location);
            }
            if (auto evaluated = evaluate(bound->operands[0], scope); !evaluated) {
                return evaluated;
            }
            // The value is evaluated where the local is written, read or not.
            auto value = lower(bound->operands[0], scope);
            if (!value) {
                return std::unexpected(value.error());
            }
            const auto required = membership(program_, bound->declared, *value);
            if (!required) {
                return std::unexpected(required.error());
            }
            if (required->has_value()) {
                emit(scope, Origin::RefinementIntroduction,
                     function_.qualified_name + " -> " + bound->declared.refinements.front().name,
                     bound->operands[0].provenance.range, **required);
            }
            scope.versions.emplace(bound->version, &bound->operands[0]);
            return walk(bound->operands[1], std::move(scope), loops);
        }

        if (const auto* branch = std::get_if<vir::Conditional>(&expression.node)) {
            if (branch->operands.size() != 3 || !branch->operands[0].type.is_boolean()) {
                return fail("malformed conditional", location);
            }
            if (auto evaluated = evaluate(branch->operands[0], scope); !evaluated) {
                return evaluated;
            }
            auto condition = lower(branch->operands[0], scope);
            if (!condition) {
                return std::unexpected(condition.error());
            }
            Scope when_true = scope;
            when_true.events.emplace_back(kernel::predicate(*condition, true));
            if (auto walked = walk(branch->operands[1], std::move(when_true), loops); !walked) {
                return walked;
            }
            scope.events.emplace_back(kernel::predicate(*condition, false));
            return walk(branch->operands[2], std::move(scope), loops);
        }

        if (const auto* loop = std::get_if<vir::Loop>(&expression.node)) {
            return enter(*loop, expression, std::move(scope), loops);
        }

        if (const auto* next = std::get_if<vir::Iterate>(&expression.node)) {
            return iterate(*next, expression, scope, loops);
        }

        return returned(expression, std::move(scope));
    }

    [[nodiscard]] std::string invariant_subject(const vir::Expr& loop, std::uint32_t position) const {
        return function_.qualified_name + " loop at line " + std::to_string(loop.provenance.range.begin.line) +
               " invariant " + std::to_string(position + 1);
    }

    // Entering a loop: each invariant must hold of the carried locals' values
    // here. From the head on, each carried local is a fresh value of which only
    // the invariants are supposed.
    std::expected<void, Failure> enter(const vir::Loop& loop, const vir::Expr& expression, Scope scope,
                                       std::vector<Active>& loops) {
        const source::SourceLocation& location = expression.provenance.range.begin;
        const std::size_t carried = loop.heads.size();
        if (loop.names.size() != carried || loop.operands.size() != carried + loop.invariants + 1 ||
            std::ranges::any_of(loops, [&loop](const Active& active) { return active.loop->loop == loop.loop; })) {
            return fail("malformed loop", location);
        }
        std::vector<kernel::Type> types;
        for (std::size_t index = 0; index < carried; ++index) {
            const std::uint32_t head = loop.heads[index];
            if (scope.versions.contains(head) || scope.opaque.contains(head) ||
                std::count(loop.heads.begin(), loop.heads.end(), head) != 1) {
                return fail("malformed loop", location);
            }
            const std::optional<kernel::Type> type = core_type(loop.operands[index].type);
            if (!type.has_value()) {
                return fail("'" + loop.names[index] + "' has a type the formal core does not represent", location);
            }
            types.push_back(*type);
        }
        for (std::uint32_t position = 0; position < loop.invariants; ++position) {
            if (!loop.operands[carried + position].type.is_boolean()) {
                return fail("a loop invariant must be a condition", location);
            }
        }

        for (std::uint32_t position = 0; position < loop.invariants; ++position) {
            Scope entry = scope;
            for (std::size_t index = 0; index < carried; ++index) {
                entry.versions.emplace(loop.heads[index], &loop.operands[index]);
            }
            const vir::Expr& written = loop.operands[carried + position];
            auto invariant = lower(written, entry);
            if (!invariant) {
                return std::unexpected(invariant.error());
            }
            emit(scope, Origin::LoopEntry, invariant_subject(expression, position), written.provenance.range,
                 kernel::predicate(*invariant, true));
        }

        Scope head = std::move(scope);
        for (std::size_t index = 0; index < carried; ++index) {
            head.opaque.emplace(loop.heads[index], head.binders.size());
            head.binders.push_back(types[index]);
            head.events.emplace_back(types[index]);
        }
        for (std::uint32_t position = 0; position < loop.invariants; ++position) {
            auto invariant = lower(loop.operands[carried + position], head);
            if (!invariant) {
                return std::unexpected(invariant.error());
            }
            head.events.emplace_back(kernel::predicate(*invariant, true));
        }

        loops.push_back(Active{&loop, types});
        auto walked = walk(loop.operands.back(), std::move(head), loops);
        loops.pop_back();
        return walked;
    }

    // The end of an iteration: each invariant must hold again of the values
    // the carried locals take into the next one. The invariant is stated with
    // the head values abstracted, then instantiated at those values by the
    // kernel's own substitution, so the head value an iteration started from
    // and the value it ends with are never confused.
    std::expected<void, Failure> iterate(const vir::Iterate& next, const vir::Expr& expression, const Scope& scope,
                                         const std::vector<Active>& loops) {
        const source::SourceLocation& location = expression.provenance.range.begin;
        const auto active = std::ranges::find_if(loops.rbegin(), loops.rend(), [&next](const Active& candidate) {
            return candidate.loop->loop == next.loop;
        });
        if (active == loops.rend()) {
            return fail("an iteration ends outside the loop it belongs to", location);
        }
        const vir::Loop& loop = *active->loop;
        const std::size_t carried = loop.heads.size();
        if (next.operands.size() != carried) {
            return fail("malformed iteration", location);
        }
        std::vector<kernel::Term> values;
        for (std::size_t index = 0; index < carried; ++index) {
            if (!(next.operands[index].type == loop.operands[index].type)) {
                return fail("malformed iteration", location);
            }
            auto value = lower(next.operands[index], scope);
            if (!value) {
                return std::unexpected(value.error());
            }
            values.push_back(std::move(*value));
        }
        for (std::uint32_t position = 0; position < loop.invariants; ++position) {
            OpaqueBindings holes = scope.opaque;
            for (std::size_t index = 0; index < carried; ++index) {
                holes[loop.heads[index]] = scope.binders.size() + index;
            }
            auto invariant = lower_value(loop.operands[carried + position], definitions_,
                                         scope.binders.size() + carried, &scope.calls, &scope.versions, &holes);
            if (!invariant) {
                return std::unexpected(invariant.error());
            }
            emit(scope, Origin::LoopPreservation, invariant_subject(expression, position), expression.provenance.range,
                 specialize(kernel::predicate(*invariant, true), active->carried, values));
        }
        return {};
    }

    std::expected<void, Failure> returned(const vir::Expr& expression, Scope scope) {
        const std::optional<kernel::Type> type = core_type(expression.type);
        if (!type.has_value() || !(*type == plan_.result)) {
            return fail("a returned value's type differs from the declared result type",
                        expression.provenance.range.begin);
        }
        if (auto evaluated = evaluate(expression, scope); !evaluated) {
            return evaluated;
        }
        auto value = lower(expression, scope);
        if (!value) {
            return std::unexpected(value.error());
        }
        const auto fresh = static_cast<std::uint32_t>(scope.binders.size() - plan_.parameters.size());
        emit(scope, Origin::ReturnPath, function_.qualified_name + " path " + std::to_string(++paths_),
             expression.provenance.range, kernel::instantiate(kernel::shift(plan_.postcondition, fresh, 1), *value));
        return {};
    }

    const vir::Function& function_;
    const ContractVerification& plan_;
    const Contracts& contracts_;
    const DefinitionMap& definitions_;
    const std::map<std::string, std::size_t>& established_;
    const Program& program_;
    std::size_t steps_ = 0;
    std::size_t paths_ = 0;
};

std::expected<ContractVerification, Failure> build_partial(const vir::Function& function, const Contracts& contracts,
                                                           const DefinitionMap& pure_definitions,
                                                           const std::map<std::string, std::size_t>& established,
                                                           Program& program) {
    ContractVerification plan;
    if (auto stated = state_contract(function, pure_definitions, program, plan); !stated) {
        return std::unexpected(stated.error());
    }
    plan.partial = true;
    Conditions generated(function, plan, contracts, pure_definitions, established, program);
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
    plan.identity = hasher.finish();
    plan.conditions = std::move(generated.conditions);
    for (Obligation& obligation : generated.obligations) {
        program.obligations.push_back(std::move(obligation));
    }
    return plan;
}

} // namespace

void generate_contracts(const vir::Module& module, const DefinitionMap& pure_definitions, Program& program,
                        diagnostics::Engine& engine, const std::function<std::string(const Failure&)>& explain) {
    Contracts contracts;
    // Only a function that states a contract and has a return tree is a
    // candidate. Pairing the tree with the function carries that guarantee
    // onward instead of re-asserting it at every use.
    struct Candidate {
        const vir::Function* function;
        const vir::Expr* returned_value;
    };
    std::vector<Candidate> pending;
    for (const auto& function : module.functions) {
        if (function.contract.has_value() && function.returned_value.has_value()) {
            contracts.emplace(function.symbol.usr, &function);
            pending.push_back({&function, &function.returned_value.value()});
        }
    }
    DefinitionMap definitions = pure_definitions;
    std::map<std::string, std::size_t> established;
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto candidate = pending.begin(); candidate != pending.end();) {
            const auto& function = *candidate->function;
            const auto& returned_value = *candidate->returned_value;
            std::vector<const vir::Expr*> calls;
            collect_calls(returned_value, contracts, calls);
            if (!std::ranges::all_of(calls, [&](const vir::Expr* call) {
                    return established.contains(std::get<vir::Call>(call->node).callee.usr);
                })) {
                ++candidate;
                continue;
            }
            // A loop, or a call whose contract is itself partial, leaves the
            // body without a total term; its contract is then partial too.
            const bool partial =
                contains_loop(returned_value) || std::ranges::any_of(calls, [&](const vir::Expr* call) {
                    return program.contracts[established.at(std::get<vir::Call>(call->node).callee.usr)].partial;
                });
            auto plan = partial ? build_partial(function, contracts, pure_definitions, established, program)
                                : build(function, contracts, pure_definitions, definitions, established, program);
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
    const auto is_pending = [&pending](const std::string& usr) {
        return std::ranges::any_of(pending, [&usr](const Candidate& each) { return each.function->symbol.usr == usr; });
    };
    for (const auto& candidate : pending) {
        const auto* function = candidate.function;
        std::vector<const vir::Expr*> calls;
        collect_calls(*candidate.returned_value, contracts, calls);
        std::string reason = "a verified callee is not available";
        source::SourceLocation location = function->range.begin;
        for (const vir::Expr* site : calls) {
            const auto& call = std::get<vir::Call>(site->node);
            if (established.contains(call.callee.usr)) {
                continue;
            }
            location = site->provenance.range.begin;
            if (call.callee.usr == function->symbol.usr) {
                reason = "it calls itself; recursion is not modeled, because termination is not yet verified";
            } else if (is_pending(call.callee.usr)) {
                reason = "its call to '" + call.callee_name +
                         "' is recursive or depends on recursion; recursion is not modeled, because termination is "
                         "not yet verified";
            } else {
                reason = "its callee '" + call.callee_name + "' has no established contract";
            }
            break;
        }
        report(engine, *function, Failure{reason, location, {}}, explain);
    }
}

} // namespace cppl::obligations::detail
