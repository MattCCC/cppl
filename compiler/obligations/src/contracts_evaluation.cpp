// What evaluating an expression owes on a path: capabilities, definedness,
// and the contracts of the calls it makes.

#include "aggregates.hpp"
#include "contracts_conditions.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/vir/capability.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"
#include "definedness.hpp"
#include "lowering.hpp"
#include "walks.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations::detail {

using contracts::Conditions;
using contracts::membership;
using contracts::refinement_label;
using contracts::specialize;

std::expected<void, Failure> Conditions::owe_capabilities(const vir::Call& call, const ContractVerification& callee,
                                                          const std::vector<kernel::Term>& arguments,
                                                          const Scope& scope, const vir::Expr& site) {
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
    const std::vector<vir::Capability>& held = function_.contract.has_value() ? function_.contract->capabilities : none;
    for (const vir::Capability& required : stated->capabilities) {
        const std::uint32_t position = required.place.root.id;
        if (required.place.root.kind != vir::PlaceRoot::Kind::Parameter || position >= call.arguments.size() ||
            arguments.size() != call.arguments.size()) {
            return fail("the memory capability '" + vir::describe(required) + "' of '" + call.callee_name +
                            "' does not name one of its parameters",
                        location);
        }
        const std::string kind = vir::describe(required.kind);
        const vir::Expr& actual = call.arguments[position];
        const auto* passed = std::get_if<vir::ParameterRef>(&actual.node);
        // Storage the caller hands on without holding a stated capability
        // for it: a span of a container it tracks, a live span local, or a
        // container's data pointer. The bridge certified each where the
        // call was lowered -- the container live and its storage at the
        // generation the view was formed at, and apart from any container
        // the same call passes by mutable reference (SPEC.md STDMODEL-016,
        // STDMODEL-017). A data pointer designates as many elements as the
        // container has; a span designates its own extent.
        bool certified = false;
        std::optional<kernel::Term> region;
        if (const auto* handed = std::get_if<vir::Call>(&actual.node);
            handed != nullptr && handed->library.has_value() && handed->arguments.size() == 1) {
            const vir::Expr& container = handed->arguments.front();
            if (handed->library->operation == source::LibraryOperation::ViewOf) {
                certified = true;
            } else if (handed->library->operation == source::LibraryOperation::DataOf) {
                // A span parameter's data is under the capability the
                // caller holds for that span; a tracked container's is the
                // caller's own.
                passed = std::get_if<vir::ParameterRef>(&container.node);
                certified = passed == nullptr;
                const std::optional<kernel::Type> domain = core_type(container.type);
                auto lowered = lower(container, scope);
                if (!domain || !domain->is_value() || !lowered) {
                    return fail("the length of the container whose data is passed for '" + required.place.spelling +
                                    "' is not a value the formal core represents",
                                location);
                }
                region = kernel::Term::project(*domain, 0, std::move(*lowered));
            }
        } else if (std::holds_alternative<vir::PlaceRef>(actual.node) &&
                   actual.type.representation.kind == source::RepresentationKind::Span) {
            certified = true;
        }
        if (passed == nullptr && !certified) {
            return fail("calling '" + call.callee_name + "' requires '" + kind + "' of the pointer passed for '" +
                            required.place.spelling + "', and only a pointer parameter of '" +
                            function_.qualified_name + "' whose contract states that capability can supply it",
                        actual.provenance.range.begin.is_valid() ? actual.provenance.range.begin : location);
        }
        const std::string owed = kind + "(" + (passed != nullptr ? passed->name : vir::describe(actual)) + ")";
        if (scope.unsafe.has_value()) {
            return fail("calling '" + call.callee_name + "' requires '" + owed +
                            "', which no longer holds after the unsafe block at " + scope.unsafe->file + ":" +
                            std::to_string(scope.unsafe->line) + ": what that block did to the storage was not checked",
                        location);
        }
        const vir::Capability* holding = nullptr;
        if (!certified) {
            const auto found = std::ranges::find_if(held, [&](const vir::Capability& candidate) {
                return candidate.kind == required.kind &&
                       candidate.place.root.kind == vir::PlaceRoot::Kind::Parameter &&
                       candidate.place.root.id == passed->parameter;
            });
            if (found == held.end()) {
                return fail("calling '" + call.callee_name + "' requires '" + owed +
                                "', which is not established: the contract of '" + function_.qualified_name +
                                "' states no such capability, and 'p != nullptr' does not imply it",
                            location);
            }
            holding = &*found;
        }
        const bool held_sized = holding != nullptr && !holding->extent.empty();
        if (required.extent.empty() && !held_sized && !region.has_value()) {
            continue;
        }
        const vir::Expr* owed_count = required.extent.empty() ? nullptr : &required.extent.front();
        const vir::Expr* held_count = held_sized ? &holding->extent.front() : nullptr;
        std::optional<kernel::Type> type = owed_count != nullptr   ? core_type(owed_count->type)
                                           : held_count != nullptr ? core_type(held_count->type)
                                                                   : std::nullopt;
        if (!type.has_value() && region.has_value()) {
            const std::optional<kernel::Type> domain =
                core_type(std::get<vir::Call>(actual.node).arguments.front().type);
            if (domain.has_value() && domain->is_value() &&
                !std::get<kernel::ValueType>(domain->node).projections.empty()) {
                type = std::get<kernel::ValueType>(domain->node).projections.front();
            }
        }
        if (!type || !type->is_integer()) {
            return fail("the element count of '" + owed + "' is not an integer the formal core represents", location);
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
        } else if (region.has_value()) {
            // A data pointer designates the container's elements: as many
            // as it has, at the type its length is stated at.
            const auto* length = std::get_if<kernel::Projection>(&region->node);
            if (length == nullptr || !length->domain.is_value() ||
                std::get<kernel::ValueType>(length->domain.node).projections.front() != kernel::Type{integer}) {
                return fail("calling '" + call.callee_name +
                                "' compares an element count against a container "
                                "length of another type, and the conversion "
                                "between them is not modeled",
                            location);
            }
            available = std::move(*region);
        }
        std::vector<kernel::Type> binders = callee.parameters;
        binders.emplace_back(integer);
        std::vector<kernel::Term> instantiated = arguments;
        instantiated.push_back(std::move(available));
        emit(scope, Origin::CallPrecondition, function_.qualified_name + " -> " + call.callee_name,
             site.provenance.range,
             specialize(kernel::predicate(
                            kernel::Term::primitive(kernel::PrimOp::LessEqual, integer,
                                                    {std::move(needed), kernel::Term::variable(kernel::VarIndex{0})}),
                            true),
                        binders, instantiated));
    }
    return {};
}

std::expected<void, Failure> Conditions::evaluate(const vir::Expr& expression, Scope& scope) {
    if (auto assembled = bind_aggregates(expression, scope); !assembled) {
        return assembled;
    }
    std::vector<Supposed> posts;
    if (auto called = evaluate_calls(expression, scope, posts); !called) {
        return called;
    }
    return owe_definedness(expression, scope, posts);
}

std::expected<void, Failure> Conditions::bind_aggregates(const vir::Expr& expression, Scope& scope) const {
    return bind_assembled(
        expression, scope.calls, [&](const vir::Expr& member) { return lower(member, scope); },
        [&](const vir::Expr& site, const kernel::Type& type, std::vector<kernel::Proposition> facts) {
            scope.calls.emplace(site.id.value, scope.binders.size());
            scope.binders.push_back(type);
            scope.events.emplace_back(type);
            for (kernel::Proposition& fact : facts) {
                scope.events.emplace_back(std::move(fact));
            }
        });
}

std::expected<void, Failure> Conditions::owe_definedness(const vir::Expr& expression, Scope& scope,
                                                         const std::vector<Supposed>& posts) {
    std::vector<kernel::Proposition> established;
    for (const DefinednessSite& site : definedness_sites(expression)) {
        Scope before = scope;
        std::vector<std::size_t> unsequenced;
        for (const Supposed& post : posts) {
            if (!sequenced_before(site, *post.call)) {
                unsequenced.push_back(post.event);
            }
        }
        // A supposed proposition binds nothing, so removing one leaves
        // every binder where it was.
        for (const std::size_t event : std::views::reverse(unsequenced)) {
            before.events.erase(before.events.begin() + static_cast<std::ptrdiff_t>(event));
        }
        auto condition =
            definedness_condition(site, [this, &before](const vir::Expr& term) { return lower(term, before); });
        if (!condition) {
            return std::unexpected(condition.error());
        }
        emit(before, Origin::DefinedBehavior, function_.qualified_name, site.operation->provenance.range, *condition,
             explain(site));
        established.push_back(std::move(*condition));
    }
    for (kernel::Proposition& condition : established) {
        scope.events.emplace_back(std::move(condition));
    }
    return {};
}

std::expected<void, Failure> Conditions::evaluate_calls(const vir::Expr& expression, Scope& scope,
                                                        std::vector<Supposed>& posts) {
    std::vector<const vir::Expr*> sites;
    collect_calls(expression, contracts_, sites);
    for (const vir::Expr* site : sites) {
        if (scope.calls.contains(site->id.value)) {
            continue;
        }
        const auto& call = std::get<vir::Call>(site->node);
        std::vector<kernel::Term> arguments;
        for (const vir::Expr& argument : call.arguments) {
            auto lowered = lower(argument, scope);
            if (!lowered) {
                return std::unexpected(lowered.error());
            }
            arguments.push_back(std::move(*lowered));
        }
        // A validation tests its argument against a refinement's predicate
        // at run time (SPEC.md RUNTIMECHECK-018). Nothing proves what it
        // returns: the path supposes, of a fresh result, that a true one
        // means the value tested satisfies the refinement, and that fact is
        // RUNTIME-CHECKED at this site (RUNTIMECHECK-011, RUNTIMECHECK-012).
        // On the path where it is false, nothing is supposed.
        if (call.validation.has_value()) {
            auto tested = validation_test(call, *site);
            if (!tested) {
                return std::unexpected(tested.error());
            }
            if (auto supposed = suppose_call(call, *site, {tested->base}, kernel::Type{kernel::kBoolean},
                                             std::move(arguments), tested->postcondition, scope);
                !supposed) {
                return supposed;
            }
            posts.push_back(Supposed{site, scope.events.size() - 1});
            if (std::ranges::none_of(validations, [&](const ValidationSite& known) {
                    return known.location == site->provenance.range.begin;
                })) {
                validations.push_back(ValidationSite{tested->name, tested->predicate, site->provenance.range.begin});
            }
            continue;
        }
        // A library operation is evaluated against its trusted summary
        // exactly as a verified call is against its contract: its
        // preconditions are owed here, and its postcondition is supposed of
        // fresh values (SPEC.md STDMODEL-013, STDMODEL-023, RFC 0020 §6).
        if (call.library.has_value()) {
            const LibrarySummary* summary = program_.library_summary(call.callee.usr);
            if (summary == nullptr) {
                return fail("the library operation '" + call.callee_name + "' has no stated summary",
                            site->provenance.range.begin);
            }
            if (call.arguments.size() != summary->parameters.size()) {
                return fail("library call argument count differs from its summary", site->provenance.range.begin);
            }
            for (const auto& precondition : summary->preconditions) {
                emit(scope, Origin::CallPrecondition, function_.qualified_name + " -> " + call.callee_name,
                     site->provenance.range, specialize(precondition, summary->parameters, arguments));
            }
            if (auto supposed = suppose_call(call, *site, summary->parameters, summary->result, std::move(arguments),
                                             summary->postcondition, scope);
                !supposed) {
                return supposed;
            }
            // The summary's postcondition is the last event supposed; an
            // operation that states none leaves only its result's binder.
            if (summary->postcondition.has_value()) {
                posts.push_back(Supposed{site, scope.events.size() - 1});
            }
            continue;
        }
        const auto found = established_.find(call.callee.usr);
        if (found == established_.end()) {
            return fail("'" + call.callee_name + "' has no established contract", site->provenance.range.begin);
        }
        const ContractVerification& callee = program_.contracts[found->second];
        if (call.arguments.size() != callee.parameters.size()) {
            return fail("call argument count differs from the contract", site->provenance.range.begin);
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
        if (auto supposed = suppose_call(call, *site, callee.parameters, callee.result, std::move(arguments),
                                         callee.postcondition, scope);
            !supposed) {
            return supposed;
        }
        // The callee's postcondition is the last event supposed.
        posts.push_back(Supposed{site, scope.events.size() - 1});
        if (std::ranges::find(scope.relied_on, found->second) == scope.relied_on.end()) {
            scope.relied_on.push_back(found->second);
        }
    }
    return {};
}

std::expected<Conditions::ValidationTest, Failure> Conditions::validation_test(const vir::Call& call,
                                                                               const vir::Expr& site) const {
    const source::SourceLocation& location = site.provenance.range.begin;
    if (!call.validation.has_value()) {
        return fail("a call that is no validation was read as one", location);
    }
    const RefinementPredicate* stated = program_.refinement(*call.validation);
    if (stated == nullptr || stated->parameters.size() != 1) {
        return fail("a validation names a refinement with no resolved predicate over one value", location);
    }
    if (stated->unvalidatable.has_value()) {
        return fail("refinement type '" + stated->name + "' cannot be validated at run time: " + *stated->unvalidatable,
                    location);
    }
    if (call.arguments.size() != 1 || core_type(call.arguments.front().type) != std::optional{stated->parameters[0]}) {
        return fail("a validation of '" + stated->name + "' tests one value of its base type", location);
    }
    if (core_type(site.type) != std::optional{kernel::Type{kernel::kBoolean}}) {
        return fail("a validation of '" + stated->name + "' is a bool", location);
    }
    // Over the parameter and then the result: the parameter is #1, the
    // result #0.
    const kernel::Term value = kernel::Term::variable(kernel::VarIndex{1});
    const kernel::Term result = kernel::Term::variable(kernel::VarIndex{0});
    kernel::Proposition holds = specialize(stated->predicate, stated->parameters, {value});
    return ValidationTest{stated->parameters[0],
                          kernel::Proposition::implication(kernel::predicate(result, true), std::move(holds)),
                          stated->name, stated->statement};
}

std::expected<void, Failure> Conditions::suppose_call(const vir::Call& call, const vir::Expr& site,
                                                      const std::vector<kernel::Type>& parameters,
                                                      const kernel::Type& result, std::vector<kernel::Term> arguments,
                                                      const std::optional<kernel::Proposition>& postcondition,
                                                      Scope& scope) {
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
            return fail("malformed call mutation", site.provenance.range.begin);
        const auto type = core_type(effect.declared);
        if (!type || *type != parameters[effect.argument])
            return fail("call mutation type mismatch", site.provenance.range.begin);
        effect_arguments.push_back(effect.argument);
        const auto position = post_positions.at(effect.version);
        if (auto existing = scope.opaque.find(effect.version); existing != scope.opaque.end()) {
            if (existing->second != first_post + position || scope.binders[existing->second] != *type)
                return fail("malformed shared call mutation", site.provenance.range.begin);
        } else {
            scope.opaque.emplace(effect.version, scope.binders.size());
            scope.binders.push_back(*type);
            scope.events.emplace_back(*type);
        }
        arguments[effect.argument] =
            kernel::Term::variable(kernel::VarIndex{static_cast<std::uint32_t>(post_positions.size() - position)});
    }
    scope.calls.emplace(site.id.value, scope.binders.size());
    scope.binders.push_back(result);
    scope.events.emplace_back(result);
    if (postcondition.has_value()) {
        std::vector<kernel::Type> binders = parameters;
        binders.push_back(result);
        std::vector<kernel::Term> instantiated = arguments;
        instantiated.push_back(kernel::Term::variable(kernel::VarIndex{0}));
        scope.events.emplace_back(specialize(*postcondition, binders, instantiated));
    }
    for (const auto& effect : call.effects) {
        const auto required = membership(program_, effect.declared, arguments[effect.argument]);
        if (!required)
            return std::unexpected(required.error());
        if (*required) {
            emit(scope, Origin::RefinementIntroduction,
                 function_.qualified_name + " -> " + refinement_label(effect.declared), site.provenance.range,
                 **required);
        }
    }
    return {};
}

} // namespace cppl::obligations::detail
