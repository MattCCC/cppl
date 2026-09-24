#include "cppl/obligations/contracts.hpp"

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/storage.hpp"
#include "cppl/vir/capability.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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
//
// Membership of a structural value is membership of each of its components, at
// the component's own declared type and over the projection that names it
// (SPEC.md 17.6). A record is valid exactly when its subobjects are, so a
// refined member owes its predicate wherever the whole object crosses into its
// type and supplies it wherever the whole object is known to be valid. This is
// one recursive rule rather than a member-specific one: the same call states a
// scalar's refinement, a member's, and a member of a member's.
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
    } else if (const auto* returned = std::get_if<vir::ReturnState>(&expression.node)) {
        for (const auto& operand : returned->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* unknown = std::get_if<vir::UnknownVersion>(&expression.node)) {
        for (const auto& operand : unknown->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* bound = std::get_if<vir::PlaceVersion>(&expression.node)) {
        for (const auto& operand : bound->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* bounded = std::get_if<vir::ElementBound>(&expression.node)) {
        // The extent is a term of its own, so a call inside it is a call this
        // body makes and must be collected with the rest.
        for (const auto& extent : bounded->extent)
            collect_calls(extent, contracts, calls);
        for (const auto& operand : bounded->operands)
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
    } else if (const auto* region = std::get_if<vir::UnsafeRegion>(&expression.node)) {
        // The block's own calls are not lowered; these are the path's after it.
        for (const auto& operand : region->operands)
            collect_calls(operand, contracts, calls);
    }
}

bool requires_conditions(const vir::Expr& expression) {
    if (std::holds_alternative<vir::Loop>(expression.node) ||
        std::holds_alternative<vir::ReturnState>(expression.node) ||
        std::holds_alternative<vir::UnknownVersion>(expression.node) ||
        // A bound states an obligation, which only the path walk emits.
        std::holds_alternative<vir::ElementBound>(expression.node) ||
        // So does a path claimed not to occur, which also has no value.
        std::holds_alternative<vir::PathContradiction>(expression.node) ||
        // A case split is paths, one per state, and no value.
        std::holds_alternative<vir::CaseSplit>(expression.node) ||
        // An unsafe block is not modeled, so no term states what it does.
        std::holds_alternative<vir::UnsafeRegion>(expression.node)) {
        return true;
    }
    if (const auto* bound = std::get_if<vir::PlaceVersion>(&expression.node)) {
        return std::ranges::any_of(bound->operands, requires_conditions);
    }
    if (const auto* branch = std::get_if<vir::Conditional>(&expression.node)) {
        return std::ranges::any_of(branch->operands, requires_conditions);
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
    const vir::PlaceVersion* binding; // null for a guard
    bool positive;                    // the guard's outcome on this path
};

struct Route {
    std::vector<Step> steps;
    const vir::Expr* returned;
};

bool routes(const vir::Expr& expression, std::vector<Step> steps, std::vector<Route>& result);

// The value a version was given on this path, or null if this path does not
// establish it. The steps taken so far are the path's version bindings, so this
// is the route-level counterpart of the resolution `lower_value` performs when
// it replays a read (SPEC.md 12.7).
const vir::Expr* established_value(const std::vector<Step>& steps, std::uint32_t version) {
    for (const auto& step : std::views::reverse(steps)) {
        if (step.binding != nullptr && step.binding->version == version)
            return step.value;
    }
    return nullptr;
}

// What a value denotes on this path, following reads of locals to the value
// that established them. A version's value reads only versions numbered below
// it, so following them strictly decreases the version and terminates; the
// bound is what the VIR already guarantees, not a fixed number of hops. A read
// this path does not establish - a loop's head version, a parameter - resolves
// to itself and is left opaque.
const vir::Expr& denoted_value(const vir::Expr& value, const std::vector<Step>& steps) {
    const vir::Expr* current = &value;
    for (const auto* read = std::get_if<vir::PlaceRef>(&current->node); read != nullptr;
         read = std::get_if<vir::PlaceRef>(&current->node)) {
        const vir::Expr* source = established_value(steps, read->version);
        if (source == nullptr)
            return *current;
        current = source;
    }
    return *current;
}

// Whether `version` is read anywhere inside a conditional's arm in `body`. Such
// a read places this local's value beneath that conditional in the lowered
// value, which decides which of the two conditionals the route must split on
// first.
bool reads_from_a_conditional(const vir::Expr& body, std::uint32_t version) {
    bool found = false;
    const auto visit = [&](const vir::Expr& node, bool inside, const auto& self) -> void {
        if (found)
            return;
        if (const auto* read = std::get_if<vir::PlaceRef>(&node.node)) {
            found = found || (inside && read->version == version);
            return;
        }
        if (const auto* choice = std::get_if<vir::Conditional>(&node.node)) {
            if (choice->operands.size() == 3) {
                self(choice->operands[0], inside, self);
                self(choice->operands[1], true, self);
                self(choice->operands[2], true, self);
                return;
            }
        }
        std::visit(
            [&](const auto& kind) {
                if constexpr (requires { kind.operands; }) {
                    for (const auto& operand : kind.operands)
                        self(operand, inside, self);
                } else if constexpr (requires { kind.arguments; }) {
                    for (const auto& argument : kind.arguments)
                        self(argument, inside, self);
                }
            },
            node.node);
    };
    visit(body, false, visit);
    return found;
}

// Bind a local whose value is, or denotes, a conditional. A conditional states
// one `select` term, of which neither arm's facts are known, so the route
// splits on the condition exactly as it does for a conditional in tail
// position: the local is bound to the arm this path takes, and owes its
// predicate under what that path supposes (SPEC.md 12.7).
//
// The split follows what the value denotes, so a conditional reached through
// any number of intervening locals splits just as a directly written one does.
// The local is bound to the arm itself, which is what it denotes on this route.
// An arm that is itself a conditional splits again, so only a non-conditional
// value is ever bound. Every expression a step points at is an existing
// subexpression of this tree, and resolution never crosses a version boundary:
// `established_value` takes the latest binding of that version on this path,
// which is the one current here.
//
// The order matters beyond this function. A route's conditions must correspond
// to the `select` nesting of the body's lowered value, because that nesting is
// what the proof is composed over: each `select` is discharged by the kernel's
// conditional elimination, and a leaf is proven under exactly the conditions
// that stand above it there. Splitting here in the order the conditionals are
// written keeps the two in step, since a read replays the value it was given.
bool bind_conditional(const vir::Expr& value, const vir::PlaceVersion& binding, const vir::Expr& body,
                      std::vector<Step> steps, std::vector<Route>& result) {
    const vir::Expr& denoted = denoted_value(value, steps);
    const auto* choice = std::get_if<vir::Conditional>(&denoted.node);
    if (choice == nullptr) {
        // The local denotes what this route resolved it to. That is the same
        // value a read of it replays, so binding it here states nothing new; it
        // only keeps the arm this route selected, which a `select` term over the
        // whole conditional would have discarded.
        steps.push_back(Step{&denoted, &binding, true});
        return routes(body, std::move(steps), result);
    }
    if (choice->operands.size() != 3 || !choice->operands[0].type.is_boolean())
        return false;
    for (const bool taken : {true, false}) {
        auto arm = steps;
        arm.push_back(Step{&choice->operands[0], nullptr, taken});
        if (!bind_conditional(choice->operands[taken ? 1 : 2], binding, body, std::move(arm), result))
            return false;
    }
    return true;
}

bool routes(const vir::Expr& expression, std::vector<Step> steps, std::vector<Route>& result) {
    if (result.size() >= 128)
        return false;
    if (const auto* bound = std::get_if<vir::PlaceVersion>(&expression.node)) {
        if (bound->operands.size() != 2)
            return false;
        // A conditional value states one `select` term, of which neither arm's
        // facts are known, so the route splits on its condition. What the value
        // denotes is what decides this, so a conditional reached through
        // intervening locals splits as a directly written one does.
        //
        // The split is taken here only when this binding is the outermost
        // `select` of what the body returns. A later binding that reads this one
        // from inside its own conditional puts this `select` beneath that one in
        // the lowered value, and the proof is composed over that nesting: the
        // conditions above a leaf there are what the leaf is proven under.
        // Splitting here as well would order the conditions differently from the
        // term and the composition would not line up, so it is left to the outer
        // conditional, which resolves this local's value through it (SPEC.md
        // 12.7). That case is refused today rather than proven.
        if (std::holds_alternative<vir::Conditional>(denoted_value(bound->operands[0], steps).node) &&
            !reads_from_a_conditional(bound->operands[1], bound->version)) {
            return bind_conditional(bound->operands[0], *bound, bound->operands[1], std::move(steps), result);
        }
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

std::expected<kernel::IntType, Failure> measure_domain(const vir::Expr& measure, const std::string& what);

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

// The domain one measure component ranges over. An unsigned machine type,
// ordered by its natural non-wrapping `<`, is well-founded; a signed one has no
// least element a descent could stop at, and is refused rather than given an
// assumed bound (SPEC.md 22.5).
std::expected<kernel::IntType, Failure> measure_domain(const vir::Expr& measure, const std::string& what) {
    const std::optional<kernel::Type> type = core_type(measure.type);
    if (!type.has_value() || !type->is_integer()) {
        return std::unexpected(
            Failure{what + " must be an integer the formal core represents", measure.provenance.range.begin, {}});
    }
    if (type->integer_type().signedness != kernel::Signedness::Unsigned) {
        return std::unexpected(Failure{what + " must range over a well-founded domain, so its type must be unsigned",
                                       measure.provenance.range.begin,
                                       {}});
    }
    return type->integer_type();
}

// `next` strictly below `here` in the lexicographic order of their components
// (SPEC.md TERMINATION-005): the first falls, or it stays and the rest fall.
// Each comparison is the machine's own at its component's unsigned type, and a
// lexicographic product of well-founded orders is well-founded.
kernel::Proposition lexicographically_below(const std::vector<kernel::Term>& next,
                                            const std::vector<kernel::Term>& here,
                                            const std::vector<kernel::IntType>& types) {
    const auto compare = [&](kernel::PrimOp op, std::size_t index) {
        return kernel::predicate(kernel::Term::primitive(op, types[index], {next[index], here[index]}), true);
    };
    std::size_t index = next.size() - 1;
    kernel::Proposition below = compare(kernel::PrimOp::Less, index);
    while (index > 0) {
        --index;
        below = kernel::Proposition::disjunction(
            compare(kernel::PrimOp::Less, index),
            kernel::Proposition::conjunction(compare(kernel::PrimOp::Equal, index), std::move(below)));
    }
    return below;
}

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
// responsibility (TRUST.md 12.1, 13). Whether each holds is the kernel's.
class Conditions {
  public:
    // `recursion` is the recursion group the function belongs to, as contract
    // indices, when it recurses: a call to one of them supposes its contract as
    // the induction hypothesis and owes a strictly smaller measure.
    Conditions(const vir::Function& function, const ContractVerification& plan, const Contracts& contracts,
               const DefinitionMap& definitions, const std::map<std::string, std::size_t>& established,
               const Program& program, const std::vector<std::size_t>& recursion = {})
        : function_(function),
          plan_(plan),
          contracts_(contracts),
          definitions_(definitions),
          established_(established),
          program_(program),
          recursion_(recursion) {}

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
    std::vector<PathClaim> claims;
    // Every unsafe block a path of the body passes through, in the order the
    // walk meets them, each once.
    std::vector<source::SourceLocation> unsafe_regions;
    // Every loop a path of the body enters that states no measure, each once.
    // Its termination is not established, so neither is the body's.
    std::vector<source::SourceLocation> unmeasured_loops;

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
        // The unsafe block this path passed through, if any: from there on it
        // holds none of its contract's capabilities (SPEC.md UNSAFE-003).
        std::optional<source::SourceLocation> unsafe;
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
              kernel::Proposition goal, std::vector<std::string> explanation = {}) {
        Obligation obligation;
        obligation.origin = origin;
        obligation.subject = std::move(subject);
        obligation.range = range;
        obligation.goal = close(scope, std::move(goal));
        obligation.explanation = std::move(explanation);
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

    // The memory capabilities a callee's contract states are owed at every call
    // to it, as its preconditions are (SPEC.md 12.10 VERIFIED-043, RFC 0014 §3).
    // A capability is not a proposition the kernel sees, so what is owed is that
    // the caller holds it: the argument is one of the caller's own pointer
    // parameters, and the caller's contract states a capability of the same kind
    // on it. Nothing about the argument's value supplies one, and neither kind
    // entails the other.
    //
    // A sized capability bounds a value, so where either side states an element
    // count, the callee's must not exceed the caller's. That comparison is an
    // ordinary obligation the kernel decides; an unstated count is one object.
    std::expected<void, Failure> owe_capabilities(const vir::Call& call, const ContractVerification& callee,
                                                  const std::vector<kernel::Term>& arguments, const Scope& scope,
                                                  const vir::Expr& site) {
        const source::SourceLocation& location = site.provenance.range.begin;
        const auto declared = contracts_.find(call.callee.usr);
        if (declared == contracts_.end()) {
            return fail("'" + call.callee_name + "' has no established contract", location);
        }
        const std::optional<vir::Contract>& stated = declared->second->contract;
        if (!stated.has_value()) {
            return fail("'" + call.callee_name + "' has no established contract", location);
        }
        static const std::vector<vir::Capability> none;
        const std::vector<vir::Capability>& held =
            function_.contract.has_value() ? function_.contract->capabilities : none;
        for (const vir::Capability& required : stated->capabilities) {
            const std::uint32_t position = required.place.root.id;
            if (required.place.root.kind != vir::PlaceRoot::Kind::Parameter || position >= call.arguments.size() ||
                arguments.size() != call.arguments.size()) {
                return fail("the memory capability '" + vir::describe(required) + "' of '" + call.callee_name +
                                "' does not name one of its parameters",
                            location);
            }
            const std::string kind = vir::describe(required.kind);
            const auto* passed = std::get_if<vir::ParameterRef>(&call.arguments[position].node);
            if (passed == nullptr) {
                return fail("calling '" + call.callee_name + "' requires '" + kind + "' of the pointer passed for '" +
                                required.place.spelling + "', and only a pointer parameter of '" +
                                function_.qualified_name + "' whose contract states that capability can supply it",
                            call.arguments[position].provenance.range.begin.is_valid()
                                ? call.arguments[position].provenance.range.begin
                                : location);
            }
            const std::string owed = kind + "(" + passed->name + ")";
            if (scope.unsafe.has_value()) {
                return fail("calling '" + call.callee_name + "' requires '" + owed +
                                "', which no longer holds after the unsafe block at " + scope.unsafe->file + ":" +
                                std::to_string(scope.unsafe->line) +
                                ": what that block did to the storage was not checked",
                            location);
            }
            const auto holding = std::ranges::find_if(held, [&](const vir::Capability& candidate) {
                return candidate.kind == required.kind &&
                       candidate.place.root.kind == vir::PlaceRoot::Kind::Parameter &&
                       candidate.place.root.id == passed->parameter;
            });
            if (holding == held.end()) {
                return fail("calling '" + call.callee_name + "' requires '" + owed +
                                "', which is not established: the contract of '" + function_.qualified_name +
                                "' states no such capability, and 'p != nullptr' does not imply it",
                            location);
            }
            if (required.extent.empty() && holding->extent.empty()) {
                continue;
            }
            const vir::Expr* owed_count = required.extent.empty() ? nullptr : &required.extent.front();
            const vir::Expr* held_count = holding->extent.empty() ? nullptr : &holding->extent.front();
            const std::optional<kernel::Type> type =
                core_type(owed_count != nullptr ? owed_count->type : held_count->type);
            if (!type || !type->is_integer()) {
                return fail("the element count of '" + owed + "' is not an integer the formal core represents",
                            location);
            }
            if (owed_count != nullptr && held_count != nullptr && core_type(held_count->type) != type) {
                return fail("calling '" + call.callee_name + "' compares an element count of type '" +
                                vir::describe(owed_count->type) + "' against one of type '" +
                                vir::describe(held_count->type) + "', and the conversion between them is not modeled",
                            location);
            }
            const kernel::IntType integer = type->integer_type();
            // The callee's count is stated over its own parameters and one more
            // binder standing for the caller's, then instantiated at the call's
            // arguments and at that count, so neither side is read in the
            // other's scope.
            kernel::Term needed = kernel::Term::literal(integer, 1);
            if (owed_count != nullptr) {
                auto lowered = lower_value(*owed_count, definitions_, callee.parameters.size() + 1);
                if (!lowered) {
                    return std::unexpected(lowered.error());
                }
                needed = std::move(*lowered);
            }
            kernel::Term available = kernel::Term::literal(integer, 1);
            if (held_count != nullptr) {
                auto lowered = lower(*held_count, scope);
                if (!lowered) {
                    return std::unexpected(lowered.error());
                }
                available = std::move(*lowered);
            }
            std::vector<kernel::Type> binders = callee.parameters;
            binders.emplace_back(integer);
            std::vector<kernel::Term> instantiated = arguments;
            instantiated.push_back(std::move(available));
            emit(scope, Origin::CallPrecondition, function_.qualified_name + " -> " + call.callee_name,
                 site.provenance.range,
                 specialize(kernel::predicate(kernel::Term::primitive(
                                                  kernel::PrimOp::LessEqual, integer,
                                                  {std::move(needed), kernel::Term::variable(kernel::VarIndex{0})}),
                                              true),
                            binders, instantiated));
        }
        return {};
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
            if (auto owed = owe_capabilities(call, callee, arguments, scope, *site); !owed) {
                return owed;
            }
            for (const auto& precondition : callee.preconditions) {
                emit(scope, Origin::CallPrecondition, function_.qualified_name + " -> " + call.callee_name,
                     site->provenance.range, specialize(precondition, callee.parameters, arguments));
            }
            // A call within the recursion group supposes the callee's contract
            // as the induction hypothesis, which holds only at a smaller
            // measure (SPEC.md TERMINATION-007).
            if (std::ranges::find(recursion_, found->second) != recursion_.end()) {
                if (auto descent = descends_at_call(call, callee, arguments, scope, *site); !descent) {
                    return descent;
                }
            }
            // All post-state values are fresh. Their facts come only from the
            // proven callee contract, never from the erased parameter type.
            std::map<std::uint32_t, std::size_t> post_positions;
            for (const auto& effect : call.effects) {
                if (!post_positions.contains(effect.version))
                    post_positions.emplace(effect.version, post_positions.size());
            }
            const auto fresh = static_cast<std::uint32_t>(post_positions.size() + 1);
            for (auto& argument : arguments)
                argument = kernel::shift(argument, fresh);
            std::vector<std::uint32_t> effect_arguments;
            const auto first_post = scope.binders.size();
            for (const auto& effect : call.effects) {
                if (effect.argument >= arguments.size() || scope.versions.contains(effect.version) ||
                    std::ranges::find(effect_arguments, effect.argument) != effect_arguments.end())
                    return fail("malformed call mutation", site->provenance.range.begin);
                const auto type = core_type(effect.declared);
                if (!type || *type != callee.parameters[effect.argument])
                    return fail("call mutation type mismatch", site->provenance.range.begin);
                effect_arguments.push_back(effect.argument);
                const auto position = post_positions.at(effect.version);
                if (auto existing = scope.opaque.find(effect.version); existing != scope.opaque.end()) {
                    if (existing->second != first_post + position || scope.binders[existing->second] != *type)
                        return fail("malformed shared call mutation", site->provenance.range.begin);
                } else {
                    scope.opaque.emplace(effect.version, scope.binders.size());
                    scope.binders.push_back(*type);
                    scope.events.emplace_back(*type);
                }
                arguments[effect.argument] = kernel::Term::variable(
                    kernel::VarIndex{static_cast<std::uint32_t>(post_positions.size() - position)});
            }
            scope.calls.emplace(site->id.value, scope.binders.size());
            scope.binders.push_back(callee.result);
            scope.events.emplace_back(callee.result);
            scope.events.emplace_back(postcondition_at(callee, arguments, kernel::Term::variable(kernel::VarIndex{0})));
            if (std::ranges::find(scope.relied_on, found->second) == scope.relied_on.end()) {
                scope.relied_on.push_back(found->second);
            }
            for (const auto& effect : call.effects) {
                const auto required = membership(program_, effect.declared, arguments[effect.argument]);
                if (!required)
                    return std::unexpected(required.error());
                if (*required)
                    emit(scope, Origin::RefinementIntroduction,
                         function_.qualified_name + " -> " + effect.declared.refinements.front().name,
                         site->provenance.range, **required);
            }
        }
        return {};
    }

    std::expected<void, Failure> walk(const vir::Expr& expression, Scope scope, std::vector<Active>& loops) {
        const source::SourceLocation& location = expression.provenance.range.begin;
        if (++steps_ > kMaxConditionSteps) {
            return fail("this body has more than " + std::to_string(kMaxConditionSteps) + " modeled steps", location);
        }

        if (const auto* unknown = std::get_if<vir::UnknownVersion>(&expression.node)) {
            const auto type = core_type(unknown->value_type);
            if (!type || unknown->operands.size() != 1 || scope.versions.contains(unknown->version) ||
                scope.opaque.contains(unknown->version))
                return fail("malformed mutation version", location);
            scope.opaque.emplace(unknown->version, scope.binders.size());
            scope.binders.push_back(*type);
            scope.events.emplace_back(*type);
            // A confined havoc keeps the one fact the place's type states: the
            // value is unknown within that type rather than unknown outright.
            // The predicate is supposed here and never owed -- whatever put a
            // value in that storage owed it where the write was modeled, so
            // demanding it again would charge the body twice for one crossing.
            if (unknown->confined) {
                auto inhabits = membership(program_, unknown->value_type, kernel::Term::variable(kernel::VarIndex{0}));
                if (!inhabits)
                    return std::unexpected(inhabits.error());
                if (*inhabits)
                    scope.events.emplace_back(std::move(**inhabits));
            }
            return walk(unknown->operands.front(), std::move(scope), loops);
        }
        if (const auto* completed = std::get_if<vir::ReturnState>(&expression.node)) {
            if (completed->operands.size() != plan_.parameters.size() + 1)
                return fail("malformed post-state", location);
            std::vector<kernel::Term> arguments;
            for (std::size_t index = 1; index < completed->operands.size(); ++index) {
                if (core_type(completed->operands[index].type) != std::optional{plan_.parameters[index - 1]})
                    return fail("post-state parameter type mismatch", location);
                auto value = lower(completed->operands[index], scope);
                if (!value)
                    return std::unexpected(value.error());
                arguments.push_back(*value);
            }
            const auto& result = completed->operands.front();
            if (core_type(result.type) != std::optional{plan_.result})
                return fail("return type mismatch", location);
            if (auto evaluated = evaluate(result, scope); !evaluated)
                return evaluated;
            auto value = lower(result, scope);
            if (!value)
                return std::unexpected(value.error());
            emit(scope, Origin::ReturnPath, function_.qualified_name + " path " + std::to_string(++paths_),
                 expression.provenance.range, postcondition_at(plan_, std::move(arguments), *value));
            return {};
        }

        if (const auto* bound = std::get_if<vir::PlaceVersion>(&expression.node)) {
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

        // A symbolic subscript owes `index < extent`. Both sides are terms, so
        // this is an ordinary proposition the kernel proves with the existing
        // arithmetic rules: bounds safety is proved, not tracked (RFC 0014 §10).
        if (const auto* bounded = std::get_if<vir::ElementBound>(&expression.node)) {
            if (bounded->operands.size() != 2 || bounded->extent.size() != 1) {
                return fail("malformed element bound", location);
            }
            if (auto evaluated = evaluate(bounded->operands[0], scope); !evaluated) {
                return evaluated;
            }
            auto index = lower(bounded->operands[0], scope);
            if (!index) {
                return std::unexpected(index.error());
            }
            const std::optional<kernel::Type> type = core_type(bounded->operands[0].type);
            if (!type || !type->is_integer()) {
                return fail("an element index must be an integer this implementation models", location);
            }
            // The extent is a term, so a dependent one -- the `N` of `T(&)[N]`
            // or the `n` of `readable(p, n)` -- states the same obligation a
            // constant one does, against a bound no integer is available for
            // here (SPEC.md STORAGE-005, TEMPLATE-001).
            const vir::Expr& stated = bounded->extent.front();
            const std::optional<kernel::Type> extent_type = core_type(stated.type);
            if (!extent_type || !extent_type->is_integer()) {
                return fail("an element extent must be an integer this implementation models", location);
            }
            // The comparison is between two terms of one type, as every other
            // modeled comparison is. A differing index and extent type is a
            // conversion this implementation does not model, and inventing one
            // here would decide the bound by a rule C++ did not state
            // (SPEC.md VERIFIED-043).
            if (!(*extent_type == *type)) {
                return fail("this subscript compares an index of type '" + vir::describe(bounded->operands[0].type) +
                                "' against an extent of type '" + vir::describe(stated.type) +
                                "', and the conversion between them is not modeled",
                            location);
            }
            if (auto evaluated = evaluate(stated, scope); !evaluated) {
                return evaluated;
            }
            auto extent = lower(stated, scope);
            if (!extent) {
                return std::unexpected(extent.error());
            }
            const kernel::IntType integer = type->integer_type();
            emit(scope, Origin::ElementBounds, function_.qualified_name + " element index",
                 bounded->operands[0].provenance.range,
                 kernel::predicate(kernel::Term::primitive(kernel::PrimOp::Less, integer, {*index, std::move(*extent)}),
                                   true));
            return walk(bounded->operands[1], std::move(scope), loops);
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

        if (const auto* claim = std::get_if<vir::PathContradiction>(&expression.node)) {
            return impossible(*claim, expression, scope);
        }

        if (const auto* split = std::get_if<vir::CaseSplit>(&expression.node)) {
            return split_path(*split, expression, std::move(scope), loops);
        }

        // An unsafe block (SPEC.md 26, INTERACT-018). It states no fact, so
        // nothing is supposed here: what it may have written already carries a
        // fresh version inside its continuation. The contract rests on it, and
        // from here on the path holds no capability.
        if (const auto* region = std::get_if<vir::UnsafeRegion>(&expression.node)) {
            if (region->operands.size() != 1) {
                return fail("malformed unsafe region", location);
            }
            if (std::ranges::find(unsafe_regions, location) == unsafe_regions.end()) {
                unsafe_regions.push_back(location);
            }
            if (!scope.unsafe.has_value()) {
                scope.unsafe = location;
            }
            return walk(region->operands.front(), std::move(scope), loops);
        }

        return returned(expression, std::move(scope));
    }

    // The first unsafe block a subtree passes through, if any.
    static std::optional<source::SourceLocation> first_unsafe_region(const vir::Expr& expression) {
        if (std::holds_alternative<vir::UnsafeRegion>(expression.node)) {
            return expression.provenance.range.begin;
        }
        return std::visit(
            [](const auto& node) -> std::optional<source::SourceLocation> {
                if constexpr (requires { node.operands; }) {
                    for (const vir::Expr& child : node.operands) {
                        if (std::optional<source::SourceLocation> found = first_unsafe_region(child)) {
                            return found;
                        }
                    }
                }
                return std::nullopt;
            },
            expression.node);
    }

    // A case split on this path (SPEC.md CASE-017). It has no runtime effect,
    // so nothing is evaluated here: the subject and the discriminators are
    // terms over the versions current where the split was written, and a later
    // write gives the place a new version they say nothing about.
    //
    // Each arm continues the path supposing exactly what the proof-side split
    // gives the same case (`prove_cases`): every earlier discriminator false and
    // its own true, the residual every one false, and, without a residual, the
    // last named case every other one false. Those suppositions cover every
    // state by excluded middle on each discriminator in turn, whatever the
    // provider said, so no path of the body is dropped. That holds only if every
    // state has exactly one arm, which is checked here as well as where the
    // split was built.
    std::expected<void, Failure> split_path(const vir::CaseSplit& split, const vir::Expr& expression, Scope scope,
                                            std::vector<Active>& loops) {
        const source::SourceLocation& location = expression.provenance.range.begin;
        const std::size_t first_arm = 1 + split.discriminators;
        if (split.arms.empty() || split.operands.size() != first_arm + split.arms.size()) {
            return fail("malformed case split", location);
        }
        if (split.product) {
            if (split.discriminators != 0 || split.arms.size() != 1 || split.arms.front().descriptor.has_value()) {
                return fail("malformed case split", location);
            }
            return walk(split.operands[first_arm], std::move(scope), loops);
        }

        constexpr std::size_t unclaimed = std::numeric_limits<std::size_t>::max();
        std::vector<std::size_t> arm_of(split.discriminators, unclaimed);
        std::size_t residual_arm = unclaimed;
        for (std::size_t position = 0; position < split.arms.size(); ++position) {
            const auto& descriptor = split.arms[position].descriptor;
            if (descriptor.has_value()) {
                if (*descriptor >= arm_of.size() || arm_of[*descriptor] != unclaimed) {
                    return fail("malformed case split", location);
                }
                arm_of[*descriptor] = position;
            } else if (!split.residual || residual_arm != unclaimed) {
                return fail("malformed case split", location);
            } else {
                residual_arm = position;
            }
        }
        if (std::ranges::find(arm_of, unclaimed) != arm_of.end() || (split.residual && residual_arm == unclaimed) ||
            (!split.residual && arm_of.empty())) {
            return fail("a case split leaves a state of its subject without an arm", location);
        }

        std::vector<kernel::Term> discriminators;
        for (std::size_t index = 0; index < split.discriminators; ++index) {
            const vir::Expr& discriminator = split.operands[1 + index];
            if (!discriminator.type.is_boolean()) {
                return fail("malformed case split", location);
            }
            auto condition = lower(discriminator, scope);
            if (!condition) {
                return std::unexpected(condition.error());
            }
            discriminators.push_back(std::move(*condition));
        }

        const std::size_t supposed = split.residual ? discriminators.size() : discriminators.size() - 1;
        for (std::size_t position = 0; position < discriminators.size(); ++position) {
            Scope arm = scope;
            for (std::size_t earlier = 0; earlier < position; ++earlier) {
                arm.events.emplace_back(kernel::predicate(discriminators[earlier], false));
            }
            if (position < supposed) {
                arm.events.emplace_back(kernel::predicate(discriminators[position], true));
            }
            if (auto walked = walk(split.operands[first_arm + arm_of[position]], std::move(arm), loops); !walked) {
                return walked;
            }
        }
        if (split.residual) {
            for (const kernel::Term& discriminator : discriminators) {
                scope.events.emplace_back(kernel::predicate(discriminator, false));
            }
            return walk(split.operands[first_arm + residual_arm], std::move(scope), loops);
        }
        return {};
    }

    // `contradiction evidence;` on this path (SPEC.md VERIFIED-023). The path
    // ends here, so nothing after it owes anything; what it owes instead is the
    // claim itself, that the facts established on the way here cannot all hold.
    // That is a condition like any other: every fresh value and supposed fact of
    // the path, closed over `False`. Its identity carries its origin, so it is
    // never mistaken for an omitted case stating the same proposition
    // (CASE-012, CASE-016).
    //
    // The evidence's arguments are specification terms and are never evaluated,
    // so a call in one is not a call this body makes: it is lowered as a term
    // like any other, and a call the core cannot state is refused rather than
    // proven where it stands.
    std::expected<void, Failure> impossible(const vir::PathContradiction& claim, const vir::Expr& expression,
                                            const Scope& scope) {
        PathClaim written;
        written.proof = claim.proof;
        written.evidence = claim.evidence;
        written.location = expression.provenance.range.begin;
        for (const vir::Expr& argument : claim.operands) {
            auto term = lower(argument, scope);
            if (!term) {
                return std::unexpected(term.error());
            }
            const std::optional<kernel::Type> type = core_type(argument.type);
            if (!type.has_value()) {
                return fail("an argument of '" + claim.evidence + "' has a type the formal core does not represent",
                            argument.provenance.range.begin);
            }
            written.arguments.push_back(std::move(*term));
            written.argument_types.push_back(*type);
        }

        // An omission in a split's arm claims its case cannot occur here, an
        // obligation of its own kind even though its mechanism is a claim's
        // (SPEC.md CASE-012, CASE-016).
        Obligation obligation;
        obligation.origin = claim.omitted.has_value() ? Origin::OmittedCase : Origin::ImpossiblePath;
        obligation.subject = claim.omitted.has_value() ? "case '" + *claim.omitted + "' of verified function '" +
                                                             function_.qualified_name + "'"
                                                       : function_.qualified_name + " path " + std::to_string(++paths_);
        obligation.range = expression.provenance.range;
        obligation.goal = close(scope, kernel::Proposition::falsity());
        source::Hasher hasher;
        hasher.update_field("partial-correctness-v1");
        hasher.update_field(identify_impossibility(obligation.origin, program_.context, obligation.subject,
                                                   obligation.goal, impossibilities_++)
                                .digest.to_short_hex(64));
        for (const std::size_t callee : scope.relied_on) {
            hasher.update_field(identity_of(callee));
        }
        obligation.id = ObligationId{hasher.finish()};

        written.obligation = program_.obligations.size() + obligations.size();
        conditions.push_back(VerificationCondition{written.obligation, scope.relied_on});
        obligations.push_back(std::move(obligation));
        claims.push_back(std::move(written));
        return {};
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
        if (loop.places.size() != carried || loop.operands.size() != carried + loop.invariants + loop.measures + 1 ||
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
                return fail("'" + describe(loop.places[index]) + "' has a type the formal core does not represent",
                            location);
            }
            types.push_back(*type);
        }
        for (std::uint32_t position = 0; position < loop.invariants; ++position) {
            if (!loop.operands[carried + position].type.is_boolean()) {
                return fail("a loop invariant must be a condition", location);
            }
        }
        // Each measure component ranges over a well-founded domain (SPEC.md
        // 22.5). An unsigned machine type ordered by its natural non-wrapping
        // `<` is one; a signed one is not, because it has no least element the
        // descent can stop at, and it is refused rather than given an assumed
        // bound. A lexicographic product of such orders is well-founded too.
        for (std::uint32_t position = 0; position < loop.measures; ++position) {
            const vir::Expr& measure = loop.operands[carried + loop.invariants + position];
            if (auto domain = measure_domain(measure, "a loop measure"); !domain) {
                return std::unexpected(domain.error());
            }
        }
        if (loop.measures == 0 && std::ranges::find(unmeasured_loops, location) == unmeasured_loops.end()) {
            unmeasured_loops.push_back(location);
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
        // An iteration may follow one that passed through an unsafe block in
        // the loop, and so may what follows the loop: neither holds a
        // capability (SPEC.md UNSAFE-003).
        if (!head.unsafe.has_value()) {
            // The iteration is the true arm of the head's condition. Where the
            // head has another shape, all of it is searched, which errs toward
            // holding fewer capabilities, never more.
            const vir::Expr& looped = loop.operands.back();
            const auto* condition = std::get_if<vir::Conditional>(&looped.node);
            head.unsafe = first_unsafe_region(
                condition != nullptr && condition->operands.size() == 3 ? condition->operands[1] : looped);
        }
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
        if (loop.measures > 0) {
            if (auto descent = descends(loop, expression, scope, *active, values); !descent) {
                return descent;
            }
        }
        return {};
    }

    [[nodiscard]] std::string measure_subject(const vir::Expr& loop) const {
        return function_.qualified_name + " loop at line " + std::to_string(loop.provenance.range.begin.line) +
               " measure";
    }

    // The descent an iteration owes its measure: the tuple it carries into the
    // next iteration is strictly below the tuple the head started from, in the
    // lexicographic order of its components (SPEC.md 24.3, LOOP-006,
    // TERMINATION-005).
    //
    // Each component is read twice from one expression. At the head it is
    // stated over the carried values as they are bound there; at the end of the
    // iteration it is stated over the values the iteration produces, by the
    // same abstraction and instantiation the invariants use, so the two points
    // are never confused. Each comparison is the machine's own order at the
    // component's unsigned type, which is well-founded, so a strict descent
    // cannot continue forever.
    std::expected<void, Failure> descends(const vir::Loop& loop, const vir::Expr& expression, const Scope& scope,
                                          const Active& active, const std::vector<kernel::Term>& values) {
        const std::size_t carried = loop.heads.size();
        OpaqueBindings holes = scope.opaque;
        for (std::size_t index = 0; index < carried; ++index) {
            holes[loop.heads[index]] = scope.binders.size() + index;
        }
        std::vector<kernel::Term> next;
        std::vector<kernel::Term> here;
        std::vector<kernel::IntType> types;
        for (std::uint32_t position = 0; position < loop.measures; ++position) {
            const vir::Expr& written = loop.operands[carried + loop.invariants + position];
            auto domain = measure_domain(written, "a loop measure");
            if (!domain) {
                return std::unexpected(domain.error());
            }
            auto after = lower_value(written, definitions_, scope.binders.size() + carried, &scope.calls,
                                     &scope.versions, &holes);
            if (!after) {
                return std::unexpected(after.error());
            }
            auto before = lower(written, scope);
            if (!before) {
                return std::unexpected(before.error());
            }
            next.push_back(std::move(*after));
            // `next` stays abstracted over the head values, so the kernel
            // instantiates it at what this iteration produced; `here` is moved
            // past those binders to stand beside it.
            here.push_back(kernel::shift(*before, static_cast<std::uint32_t>(carried)));
            types.push_back(*domain);
        }
        std::string measure;
        for (std::uint32_t position = 0; position < loop.measures; ++position) {
            measure +=
                (measure.empty() ? "" : ", ") + vir::describe(loop.operands[carried + loop.invariants + position]);
        }
        std::string carries;
        if (const auto* iteration = std::get_if<vir::Iterate>(&expression.node)) {
            for (std::size_t index = 0; index < carried && index < iteration->operands.size(); ++index) {
                carries += (carries.empty() ? "" : ", ") + vir::describe(loop.places[index]) + " = " +
                           vir::describe(iteration->operands[index]);
            }
        }
        emit(scope, Origin::LoopDescent, measure_subject(expression),
             loop.operands[carried + loop.invariants].provenance.range,
             specialize(lexicographically_below(next, here, types), active.carried, values),
             {"the measure is (" + measure + ") at the head of the iteration, and is read again where this path " +
                  (carried == 0 ? std::string("ends it, having changed nothing it reads") : "ends it, at " + carries),
              "the second reading must be strictly smaller, component by component in order; 'x#k' names one "
              "value local 'x' takes on the path"});
        return {};
    }

    // The descent a call within the recursion group owes: the callee's measure
    // at the call's arguments is strictly below the caller's at the values it
    // was entered with, lexicographically (SPEC.md TERMINATION-005,
    // TERMINATION-007). Both are read from the contracts, so a measure is a
    // function of the parameters alone, which a body cannot write.
    //
    // The callee's measure is stated over its own parameters and instantiated
    // at the arguments by the kernel's substitution; the caller's is moved past
    // those binders to stand beside it, as a loop's head measure is.
    std::expected<void, Failure> descends_at_call(const vir::Call& call, const ContractVerification& callee,
                                                  const std::vector<kernel::Term>& arguments, const Scope& scope,
                                                  const vir::Expr& site) {
        const source::SourceLocation& location = site.provenance.range.begin;
        const auto declared = contracts_.find(call.callee.usr);
        if (declared == contracts_.end()) {
            return fail("'" + call.callee_name + "' has no established contract", location);
        }
        const std::optional<vir::Contract>& own = function_.contract;
        const std::optional<vir::Contract>& stated = declared->second->contract;
        if (!own.has_value() || !stated.has_value()) {
            return fail("'" + call.callee_name + "' has no established contract", location);
        }
        const std::vector<vir::Expr>& mine = own->measures;
        const std::vector<vir::Expr>& theirs = stated->measures;
        if (mine.empty() || mine.size() != theirs.size()) {
            return fail("'" + function_.qualified_name + "' and '" + call.callee_name +
                            "' call each other, so each states a measure with as many components as the other",
                        location);
        }
        std::vector<kernel::Term> next;
        std::vector<kernel::Term> here;
        std::vector<kernel::IntType> types;
        for (std::size_t position = 0; position < mine.size(); ++position) {
            auto caller = measure_domain(mine[position], "a function measure");
            if (!caller) {
                return std::unexpected(caller.error());
            }
            auto called = measure_domain(theirs[position], "a function measure");
            if (!called) {
                return std::unexpected(called.error());
            }
            if (!(*caller == *called)) {
                return fail("component " + std::to_string(position + 1) + " of the measure of '" +
                                function_.qualified_name + "' has type '" + vir::describe(mine[position].type) +
                                "', and of '" + call.callee_name + "' type '" + vir::describe(theirs[position].type) +
                                "': the two cannot be compared",
                            location);
            }
            auto after = lower_value(theirs[position], definitions_, callee.parameters.size());
            if (!after) {
                return std::unexpected(after.error());
            }
            auto before = lower(mine[position], scope);
            if (!before) {
                return std::unexpected(before.error());
            }
            next.push_back(std::move(*after));
            here.push_back(kernel::shift(*before, static_cast<std::uint32_t>(callee.parameters.size())));
            types.push_back(*caller);
        }
        const auto described = [](const std::vector<vir::Expr>& expressions) {
            std::string text;
            for (const vir::Expr& expression : expressions) {
                text += (text.empty() ? "" : ", ") + vir::describe(expression);
            }
            return "(" + text + ")";
        };
        emit(scope, Origin::CallDescent, function_.qualified_name + " -> " + call.callee_name, site.provenance.range,
             specialize(lexicographically_below(next, here, types), callee.parameters, arguments),
             {"'" + function_.qualified_name + "' was entered at measure " + described(mine) + "; '" +
                  call.callee_name + "' is called with arguments " + described(call.arguments) + ", at its measure " +
                  described(theirs),
              "the call's measure must be strictly smaller, component by component in order"});
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
    const std::vector<std::size_t>& recursion_;
    std::size_t steps_ = 0;
    std::size_t paths_ = 0;
    std::size_t impossibilities_ = 0;
};

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
    for (Obligation& obligation : generated.obligations) {
        program.obligations.push_back(std::move(obligation));
    }
    std::ranges::move(generated.claims, std::back_inserter(program.path_claims));
    return hasher.finish();
}

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

// The strongly connected components of a call graph of `count` functions, each
// component's members in ascending order and the components ordered by their
// first member, so the result does not depend on how the search walked.
// Iterative, so a long call chain cannot exhaust the stack.
std::vector<std::vector<std::size_t>> recursion_groups(
    std::size_t count, const std::function<const std::vector<std::size_t>&(std::size_t)>& callees) {
    constexpr std::size_t kUnvisited = static_cast<std::size_t>(-1);
    std::vector<std::size_t> order(count, kUnvisited);
    std::vector<std::size_t> low(count, 0);
    std::vector<bool> on_stack(count, false);
    std::vector<std::size_t> stack;
    std::vector<std::vector<std::size_t>> groups;
    std::size_t next = 0;
    struct Frame {
        std::size_t node;
        std::size_t edge;
    };
    for (std::size_t root = 0; root < count; ++root) {
        if (order[root] != kUnvisited) {
            continue;
        }
        std::vector<Frame> frames{{root, 0}};
        order[root] = low[root] = next++;
        stack.push_back(root);
        on_stack[root] = true;
        while (!frames.empty()) {
            Frame& frame = frames.back();
            const std::vector<std::size_t>& edges = callees(frame.node);
            if (frame.edge < edges.size()) {
                const std::size_t callee = edges[frame.edge++];
                if (order[callee] == kUnvisited) {
                    order[callee] = low[callee] = next++;
                    stack.push_back(callee);
                    on_stack[callee] = true;
                    frames.push_back({callee, 0});
                } else if (on_stack[callee]) {
                    low[frame.node] = std::min(low[frame.node], order[callee]);
                }
                continue;
            }
            const std::size_t node = frame.node;
            frames.pop_back();
            if (!frames.empty()) {
                low[frames.back().node] = std::min(low[frames.back().node], low[node]);
            }
            if (low[node] == order[node]) {
                std::vector<std::size_t>& group = groups.emplace_back();
                std::size_t member = kUnvisited;
                while (member != node) {
                    member = stack.back();
                    stack.pop_back();
                    on_stack[member] = false;
                    group.push_back(member);
                }
                std::ranges::sort(group);
            }
        }
    }
    // Disjoint and each sorted, so their lexicographic order is that of their
    // first members.
    std::ranges::sort(groups);
    return groups;
}

// A function whose termination was asked for, or is required, and is not
// established (SPEC.md TERMINATION-006): a verification failure, never an
// omission.
void report_termination(diagnostics::Engine& engine, const vir::Function& function, const std::string& reason,
                        const source::SourceLocation& location, std::string note) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = diagnostics::Category::ProofFailure;
    diagnostic.location = location.is_valid() ? location : function.range.begin;
    diagnostic.message =
        "the termination of verified function '" + function.qualified_name + "' is not established: " + reason;
    diagnostic.notes.push_back({std::move(note), diagnostic.location});
    engine.report(std::move(diagnostic));
}

std::string written_at(const source::SourceLocation& location) {
    return location.file + ":" + std::to_string(location.line);
}

// Which contracts are total-correctness claims (SPEC.md CORRECT-003 to
// CORRECT-006). One built as a theorem about its definition has no loop and
// calls only such contracts, so it is total. One built from conditions is total
// when every loop its paths enter states a measure, it passes through no
// unsafe block, and every contract it calls is total: the greatest fixed point
// of that rule, so a recursion group is total when all of its members are,
// their calls within it descending a measure. A function whose `decreases`
// asks that it terminate and is not total is refused, for the first reason it
// is not.
void settle_totality(Program& program, const Contracts& contracts, diagnostics::Engine& engine) {
    const std::size_t count = program.contracts.size();
    std::map<std::uint32_t, std::size_t> index_of;
    for (std::size_t index = 0; index < count; ++index) {
        index_of.emplace(program.contracts[index].function.value, index);
    }
    std::vector<std::vector<std::size_t>> callees(count);
    const auto calls = [&callees](std::size_t caller, std::size_t callee) {
        if (std::ranges::find(callees[caller], callee) == callees[caller].end()) {
            callees[caller].push_back(callee);
        }
    };
    std::vector<bool> total(count, false);
    for (std::size_t index = 0; index < count; ++index) {
        const ContractVerification& contract = program.contracts[index];
        if (!contract.partial) {
            total[index] = true;
            for (const ReturnPath& path : contract.paths) {
                for (const CallVerification& call : path.calls) {
                    if (const auto callee = index_of.find(call.callee.value); callee != index_of.end()) {
                        calls(index, callee->second);
                    }
                }
            }
            continue;
        }
        total[index] = contract.unmeasured_loops.empty() && contract.unsafe_regions.empty();
        for (const VerificationCondition& condition : contract.conditions) {
            for (const std::size_t callee : condition.callees) {
                if (callee < count) {
                    calls(index, callee);
                }
            }
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t index = 0; index < count; ++index) {
            if (total[index] &&
                std::ranges::any_of(callees[index], [&total](std::size_t callee) { return !total[callee]; })) {
                total[index] = false;
                changed = true;
            }
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        program.contracts[index].total = total[index];
    }

    for (const auto& [usr, function] : contracts) {
        const auto found = index_of.find(function->id.value);
        if (found == index_of.end() || !function->contract.has_value() || function->contract->measures.empty() ||
            total[found->second]) {
            continue;
        }
        const ContractVerification& contract = program.contracts[found->second];
        std::string reason;
        source::SourceLocation location = function->contract->measure_range.begin;
        if (!contract.unmeasured_loops.empty()) {
            location = contract.unmeasured_loops.front();
            reason = "the loop at " + written_at(location) + " states no measure";
        } else if (!contract.unsafe_regions.empty()) {
            location = contract.unsafe_regions.front();
            reason = "it passes through the unsafe block at " + written_at(location) + ", which need not return";
        } else {
            const auto callee = std::ranges::find_if(callees[found->second],
                                                     [&total](std::size_t candidate) { return !total[candidate]; });
            reason = callee == callees[found->second].end()
                         ? "a function it calls does not terminate"
                         : "it calls '" + program.contracts[*callee].name + "', whose termination is not established";
        }
        report_termination(engine, *function, reason, location,
                           "a 'decreases' clause makes termination part of what is verified, so every loop the "
                           "function runs states a measure and every function it calls terminates (SPEC.md "
                           "TERMINATION-006, CORRECT-004)");
    }
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
        std::vector<const vir::Expr*> calls; // every verified call the body makes
        std::vector<std::size_t> callees;    // the candidates those calls reach, each once
    };
    std::vector<Candidate> candidates;
    for (const auto& function : module.functions) {
        if (function.contract.has_value() && function.returned_value.has_value()) {
            contracts.emplace(function.symbol.usr, &function);
            candidates.push_back({&function, &function.returned_value.value(), {}, {}});
        }
    }
    std::map<std::string, std::size_t> position_of;
    for (std::size_t position = 0; position < candidates.size(); ++position) {
        position_of.emplace(candidates[position].function->symbol.usr, position);
    }
    for (Candidate& candidate : candidates) {
        collect_calls(*candidate.returned_value, contracts, candidate.calls);
        for (const vir::Expr* site : candidate.calls) {
            const std::size_t callee = position_of.at(std::get<vir::Call>(site->node).callee.usr);
            if (std::ranges::find(candidate.callees, callee) == candidate.callees.end()) {
                candidate.callees.push_back(callee);
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
                const std::size_t callee = position_of.at(std::get<vir::Call>(site->node).callee.usr);
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
    const auto ready = [&](const Unit& unit) {
        return std::ranges::all_of(unit.members, [&](std::size_t member) {
            return std::ranges::all_of(candidates[member].callees, [&](std::size_t callee) {
                return std::ranges::find(unit.members, callee) != unit.members.end() ||
                       established.contains(candidates[callee].function->symbol.usr);
            });
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
            // A loop, or a call whose contract is itself partial, leaves the
            // body without a total term; its contract is then partial too. So
            // does a call whose callee states memory capabilities: what such a
            // call owes is checked where the path makes it (SPEC.md 12.10
            // VERIFIED-043), which only the conditions walk does.
            const bool partial = requires_conditions(*candidate.returned_value) ||
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

} // namespace cppl::obligations::detail
