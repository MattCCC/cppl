// The conditions each path of a verified body establishes, walked statement
// by statement, and the plans built from them.

#include "contracts_conditions.hpp"

#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"
#include "definedness.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations::detail {

using contracts::Conditions;
using contracts::kMaxConditionSteps;
using contracts::measure_domain;
using contracts::membership;
using contracts::owed_at_return;
using contracts::quantify;
using contracts::refinement_label;
using contracts::specialize;

std::expected<void, Failure> Conditions::run() {
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

std::unexpected<Failure> Conditions::fail(std::string reason, const source::SourceLocation& location) {
    return std::unexpected(Failure{std::move(reason), location, {}});
}

kernel::Proposition Conditions::close(const Scope& scope, kernel::Proposition goal) const {
    for (const auto& event : std::ranges::reverse_view(scope.events)) {
        if (const auto* binder = std::get_if<kernel::Type>(&event)) {
            goal = kernel::Proposition::for_all(*binder, std::move(goal));
        } else {
            goal = kernel::Proposition::implication(std::get<kernel::Proposition>(event), std::move(goal));
        }
    }
    return quantify(plan_.parameters, std::move(goal));
}

std::expected<kernel::Term, Failure> Conditions::lower(const vir::Expr& expression, const Scope& scope) const {
    return lower_value(expression, definitions_, scope.binders.size(), &scope.calls, &scope.versions, &scope.opaque);
}

std::string Conditions::identity_of(std::size_t index) const {
    const ContractVerification& callee = program_.contracts[index];
    return callee.partial ? callee.identity.to_short_hex(64)
                          : program_.obligations[callee.obligation].id.digest.to_short_hex(64);
}

void Conditions::emit(const Scope& scope, Origin origin, std::string subject, const source::SourceRange& range,
                      kernel::Proposition goal, std::vector<std::string> explanation) {
    Obligation obligation;
    obligation.origin = origin;
    obligation.subject = std::move(subject);
    obligation.range = range;
    obligation.goal = close(scope, std::move(goal));
    obligation.explanation = std::move(explanation);
    source::Hasher hasher;
    hasher.update_field("partial-correctness-v1");
    hasher.update_field(identify_goal(program_.context, obligation.subject, obligation.goal).digest.to_short_hex(64));
    for (const std::size_t callee : scope.relied_on) {
        hasher.update_field(identity_of(callee));
    }
    obligation.id = ObligationId{hasher.finish()};
    conditions.push_back(VerificationCondition{program_.obligations.size() + obligations.size(), scope.relied_on});
    obligations.push_back(std::move(obligation));
}

std::expected<void, Failure> Conditions::walk(const vir::Expr& expression, Scope scope, std::vector<Active>& loops) {
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
        const auto& result = completed->operands.front();
        if (core_type(result.type) != std::optional{plan_.result})
            return fail("return type mismatch", location);
        // The calls the returned value makes are evaluated first. Each adds
        // the binders of its result and post-state to the scope, so every
        // term the obligation states must be lowered after them: a
        // post-state lowered before would name the binder that stood at its
        // position then, which is a call's result once the call is in scope.
        // A call left in the returned value writes nothing -- the lowering
        // binds one with effects before the return -- so evaluating it
        // first changes no post-state (SPEC.md VERIFIED-031).
        if (auto evaluated = evaluate(result, scope); !evaluated)
            return evaluated;
        std::vector<kernel::Term> arguments;
        for (std::size_t index = 1; index < completed->operands.size(); ++index) {
            if (core_type(completed->operands[index].type) != std::optional{plan_.parameters[index - 1]})
                return fail("post-state parameter type mismatch", location);
            auto value = lower(completed->operands[index], scope);
            if (!value)
                return std::unexpected(value.error());
            arguments.push_back(*value);
        }
        auto value = lower(result, scope);
        if (!value)
            return std::unexpected(value.error());
        emit(scope, Origin::ReturnPath, function_.qualified_name + " path " + std::to_string(++paths_),
             expression.provenance.range, owed_at_return(plan_, std::move(arguments), *value));
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
                 function_.qualified_name + " -> " + refinement_label(bound->declared),
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
        // A constant index the bridge folded has no source of its own.
        const source::SourceRange& subscript = bounded->operands[0].provenance.range.begin.is_valid()
                                                   ? bounded->operands[0].provenance.range
                                                   : expression.provenance.range;
        // A signed index names an element only where it is not negative as
        // well: `a[-1]` is outside the array whatever its extent, so `i < n`
        // alone is not the bound (C++ [expr.sub], SPEC.md STORAGE-005).
        kernel::Proposition bound = kernel::predicate(
            kernel::Term::primitive(kernel::PrimOp::Less, integer, {*index, std::move(*extent)}), true);
        if (integer.signedness == kernel::Signedness::Signed) {
            bound = kernel::Proposition::conjunction(
                kernel::predicate(kernel::Term::primitive(kernel::PrimOp::LessEqual, integer,
                                                          {kernel::Term::literal(integer, 0), *index}),
                                  true),
                std::move(bound));
        }
        emit(scope, Origin::ElementBounds, function_.qualified_name + " element index", subscript, std::move(bound));
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

std::optional<source::SourceLocation> Conditions::first_unsafe_region(const vir::Expr& expression) {
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

std::expected<void, Failure> Conditions::split_path(const vir::CaseSplit& split, const vir::Expr& expression,
                                                    Scope scope, std::vector<Active>& loops) {
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

std::expected<void, Failure> Conditions::impossible(const vir::PathContradiction& claim, const vir::Expr& expression,
                                                    const Scope& path) {
    PathClaim written;
    written.proof = claim.proof;
    written.evidence = claim.evidence;
    written.location = expression.provenance.range.begin;
    // A struct value an argument assembles is bound as an evaluation binds it,
    // and the claim is closed over it (TRUST.md TCB-AGGREGATE-001).
    Scope scope = path;
    for (const vir::Expr& argument : claim.operands) {
        if (auto assembled = bind_aggregates(argument, scope); !assembled) {
            return assembled;
        }
    }
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
    obligation.subject = claim.omitted.has_value()
                             ? "case '" + *claim.omitted + "' of verified function '" + function_.qualified_name + "'"
                             : function_.qualified_name + " path " + std::to_string(++paths_);
    obligation.range = expression.provenance.range;
    obligation.goal = close(scope, kernel::Proposition::falsity());
    source::Hasher hasher;
    hasher.update_field("partial-correctness-v1");
    hasher.update_field(identify_impossibility(obligation.origin, program_.context, obligation.subject, obligation.goal,
                                               impossibilities_++)
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

std::string Conditions::invariant_subject(const vir::Expr& loop, std::uint32_t position) const {
    return function_.qualified_name + " loop at line " + std::to_string(loop.provenance.range.begin.line) +
           " invariant " + std::to_string(position + 1);
}

std::expected<void, Failure> Conditions::enter(const vir::Loop& loop, const vir::Expr& expression, Scope scope,
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
        const vir::Type& stated = loop.operands[carried + position].type;
        if (!stated.is_boolean() && !stated.is_proposition()) {
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
        // An invariant is a specification: it states that its operations
        // are defined as well as that it holds (SPEC.md ARITH-010).
        auto invariant = specified(written, [this, &entry](const vir::Expr& term) { return lower(term, entry); });
        if (!invariant) {
            return std::unexpected(invariant.error());
        }
        emit(scope, Origin::LoopEntry, invariant_subject(expression, position), written.provenance.range,
             std::move(*invariant));
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
        auto invariant = specified(loop.operands[carried + position],
                                   [this, &head](const vir::Expr& term) { return lower(term, head); });
        if (!invariant) {
            return std::unexpected(invariant.error());
        }
        head.events.emplace_back(std::move(*invariant));
    }

    loops.push_back(Active{&loop, types});
    auto walked = walk(loop.operands.back(), std::move(head), loops);
    loops.pop_back();
    return walked;
}

std::expected<void, Failure> Conditions::iterate(const vir::Iterate& next, const vir::Expr& expression,
                                                 const Scope& scope, const std::vector<Active>& loops) {
    const source::SourceLocation& location = expression.provenance.range.begin;
    const auto active = std::ranges::find_if(
        loops.rbegin(), loops.rend(), [&next](const Active& candidate) { return candidate.loop->loop == next.loop; });
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
        auto invariant = specified(loop.operands[carried + position], [&](const vir::Expr& term) {
            return lower_value(term, definitions_, scope.binders.size() + carried, &scope.calls, &scope.versions,
                               &holes);
        });
        if (!invariant) {
            return std::unexpected(invariant.error());
        }
        emit(scope, Origin::LoopPreservation, invariant_subject(expression, position), expression.provenance.range,
             specialize(std::move(*invariant), active->carried, values));
    }
    if (loop.measures > 0) {
        if (auto descent = descends(loop, expression, scope, *active, values); !descent) {
            return descent;
        }
    }
    return {};
}

} // namespace cppl::obligations::detail
