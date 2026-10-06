// Elaborating expressions into VIR, with the places, types and operators
// they convert to.

#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"
#include "elaborate_detail.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::elaboration {

using detail::elaborator::convert_operator;
using detail::elaborator::convert_place;
using detail::elaborator::convert_type;
using detail::elaborator::ExpressionElaborator;
using detail::elaborator::ResolvedClaim;

namespace detail::elaborator {

// The bridge's place, as VIR carries it. Both layers describe the same storage
// (SPEC.md 12.10); they are separate types because the bridge boundary does not
// let a Clang-facing structure reach the logical core (ARCHITECTURE.md 11).
vir::Place convert_place(const clangbridge::Place& place) {
    using From = clangbridge::PlaceRoot::Kind;
    using To = vir::PlaceRoot::Kind;
    vir::Place converted;
    switch (place.root.kind) {
        case From::Parameter:
            converted.root.kind = To::Parameter;
            break;
        case From::Deref:
            converted.root.kind = To::Deref;
            break;
        case From::Local:
            converted.root.kind = To::Local;
            break;
    }
    converted.root.id = place.root.id;
    converted.root.version = place.root.version;
    converted.spelling = place.spelling;
    converted.path.reserve(place.path.size());
    for (const auto& step : place.path) {
        vir::PlaceStep converted_step;
        switch (step.kind) {
            case clangbridge::PlaceStep::Kind::Element:
                converted_step.kind = vir::PlaceStep::Kind::Element;
                break;
            case clangbridge::PlaceStep::Kind::SymbolicElement:
                converted_step.kind = vir::PlaceStep::Kind::SymbolicElement;
                break;
            case clangbridge::PlaceStep::Kind::Field:
                converted_step.kind = vir::PlaceStep::Kind::Field;
                break;
        }
        converted_step.index = step.index;
        converted_step.symbol = step.symbol;
        converted.path.push_back(converted_step);
    }
    return converted;
}

std::optional<vir::Type> convert_type(const clangbridge::Type& type) {
    std::optional<vir::Type> converted;
    switch (type.kind) {
        case clangbridge::TypeKind::Void:
            converted = vir::Type::void_type();
            break;
        case clangbridge::TypeKind::Int:
            converted = vir::Type::integer(type.width, type.is_signed);
            break;
        case clangbridge::TypeKind::Bool:
            converted = vir::Type::boolean();
            break;
        case clangbridge::TypeKind::Proposition:
            converted = vir::Type::proposition();
            break;
        case clangbridge::TypeKind::Value:
            converted = vir::Type::value();
            for (const auto& projection : type.projections) {
                auto component = convert_type(projection);
                if (!component)
                    return std::nullopt;
                std::get<vir::ValueType>(converted->node).projections.push_back(std::move(*component));
            }
            break;
        case clangbridge::TypeKind::Unsupported:
            return std::nullopt;
    }
    // The base type is what the value is; the refinements are what is known
    // about it (SPEC.md 17). Both are carried, so verification can tell
    // `Percentage` from `int` while code generation cannot.
    for (const clangbridge::Refinement& refinement : type.refinements) {
        converted->refinements.push_back(vir::Refinement{refinement.name, refinement.arguments, refinement.identity});
    }
    // What C++ representation the value stands for, when it is one a
    // decomposition provider may model. Nothing is inferred from a spelling:
    // this is the identity Clang resolved (SPEC.md 20.1, 20.4).
    converted->representation.identity = type.representation.identity;
    converted->representation.kind = type.representation.kind;
    converted->representation.components = type.representation.components;
    converted->representation.rejection = type.representation.rejection;
    // The resolved C++ spelling is kept for every type, so a diagnostic can
    // name what the author wrote even when no provider models it.
    converted->representation.name = type.representation.name.empty() ? type.spelling : type.representation.name;
    for (const clangbridge::Enumerator& enumerator : type.representation.enumerators) {
        converted->representation.enumerators.push_back(vir::Enumerator{enumerator.name, enumerator.value});
    }
    return converted;
}

vir::BinaryOp convert_operator(clangbridge::BinaryOp op) {
    switch (op) {
        case clangbridge::BinaryOp::Add:
            return vir::BinaryOp::Add;
        case clangbridge::BinaryOp::Sub:
            return vir::BinaryOp::Sub;
        case clangbridge::BinaryOp::Mul:
            return vir::BinaryOp::Mul;
        case clangbridge::BinaryOp::Div:
            return vir::BinaryOp::Div;
        case clangbridge::BinaryOp::Rem:
            return vir::BinaryOp::Rem;
        case clangbridge::BinaryOp::Equal:
            return vir::BinaryOp::Equal;
        case clangbridge::BinaryOp::NotEqual:
            return vir::BinaryOp::NotEqual;
        case clangbridge::BinaryOp::Less:
            return vir::BinaryOp::Less;
        case clangbridge::BinaryOp::LessEqual:
            return vir::BinaryOp::LessEqual;
        case clangbridge::BinaryOp::Greater:
            return vir::BinaryOp::Greater;
        case clangbridge::BinaryOp::GreaterEqual:
            return vir::BinaryOp::GreaterEqual;
        case clangbridge::BinaryOp::And:
            return vir::BinaryOp::And;
        case clangbridge::BinaryOp::Or:
            return vir::BinaryOp::Or;
        case clangbridge::BinaryOp::Unsupported:
            break;
    }
    return vir::BinaryOp::Add;
}

} // namespace detail::elaborator

std::optional<vir::Expr> ExpressionElaborator::convert(const clangbridge::Expr& expr) {
    const std::optional<vir::Type> type = convert_type(expr.type);
    if (!type.has_value()) {
        failure_ = Failure{"type '" + expr.type.spelling + "' is not modeled", expr.location};
        return std::nullopt;
    }

    vir::Expr result;
    result.id = vir::ExprId{next_id_++};
    result.type = *type;
    result.provenance.range.begin = expr.location;

    if (const auto* projection = std::get_if<clangbridge::Projection>(&expr.node)) {
        if (projection->operands.size() != 1)
            return std::nullopt;
        auto operand = convert(projection->operands[0]);
        if (!operand)
            return std::nullopt;
        result.node = vir::Projection{projection->index, {std::move(*operand)}};
        return result;
    }
    // A struct value assembled from its members keeps one operand per component
    // of its type, in order (TRUST.md TCB-AGGREGATE-001).
    if (const auto* aggregate = std::get_if<clangbridge::Aggregate>(&expr.node)) {
        if (!type->is_value() ||
            aggregate->operands.size() != std::get<vir::ValueType>(type->node).projections.size()) {
            failure_ = Failure{"malformed struct value", expr.location};
            return std::nullopt;
        }
        vir::Aggregate converted;
        for (const auto& operand : aggregate->operands) {
            auto value = convert(operand);
            if (!value)
                return std::nullopt;
            converted.operands.push_back(std::move(*value));
        }
        result.node = std::move(converted);
        return result;
    }
    if (const auto* quantified = std::get_if<clangbridge::Universal>(&expr.node)) {
        if (quantified->binders.empty() || quantified->body.size() != 1) {
            failure_ = Failure{"malformed universal proposition", expr.location};
            return std::nullopt;
        }
        vir::Universal converted;
        for (const auto& binder : quantified->binders) {
            auto binder_type = convert_type(binder);
            // Quantification ranges over a modeled domain of values. A
            // structural value is a decomposition subject, not such a
            // domain: nothing states what its inhabitants are.
            if (!binder_type || binder_type->is_proposition() || binder_type->is_value()) {
                failure_ = Failure{"quantifier binder type '" + binder.spelling + "' is not modeled", expr.location};
                return std::nullopt;
            }
            converted.binders.push_back(*binder_type);
        }
        auto body = convert(quantified->body[0]);
        if (!body)
            return std::nullopt;
        converted.body.push_back(std::move(*body));
        result.node = std::move(converted);
        return result;
    }
    if (const auto* connective = std::get_if<clangbridge::Connective>(&expr.node)) {
        vir::Connective converted;
        switch (connective->kind) {
            case clangbridge::Connective::Kind::Conjunction:
                converted.kind = vir::Connective::Kind::Conjunction;
                break;
            case clangbridge::Connective::Kind::Disjunction:
                converted.kind = vir::Connective::Kind::Disjunction;
                break;
            case clangbridge::Connective::Kind::Equivalence:
                converted.kind = vir::Connective::Kind::Equivalence;
                break;
        }
        for (const auto& operand : connective->operands) {
            auto value = convert(operand);
            if (!value)
                return std::nullopt;
            converted.operands.push_back(std::move(*value));
        }
        result.node = std::move(converted);
        return result;
    }
    if (const auto* implication = std::get_if<clangbridge::Implication>(&expr.node)) {
        vir::Implication converted;
        for (const auto& operand : implication->operands) {
            auto value = convert(operand);
            if (!value)
                return std::nullopt;
            converted.operands.push_back(std::move(*value));
        }
        result.node = std::move(converted);
        return result;
    }

    if (const auto* equality = std::get_if<clangbridge::FormalEquality>(&expr.node)) {
        const auto operand_type = convert_type(equality->operand_type);
        if (!operand_type ||
            (!operand_type->is_integer() && !operand_type->is_boolean() && !operand_type->is_value()) ||
            equality->operands.size() != 2) {
            failure_ = Failure{"formal equality requires two operands of a modeled C++ type", expr.location};
            return std::nullopt;
        }
        vir::FormalEquality converted{*operand_type, {}};
        for (const auto& operand : equality->operands) {
            auto value = convert(operand);
            if (!value)
                return std::nullopt;
            if (!(value->type == *operand_type)) {
                failure_ = Failure{"formal equality operand has the wrong type", operand.location};
                return std::nullopt;
            }
            converted.operands.push_back(std::move(*value));
        }
        result.node = std::move(converted);
        return result;
    }

    if (const auto* completed = std::get_if<clangbridge::ReturnState>(&expr.node)) {
        vir::ReturnState converted;
        for (const auto& operand : completed->operands) {
            auto value = convert(operand);
            if (!value)
                return std::nullopt;
            converted.operands.push_back(std::move(*value));
        }
        result.node = std::move(converted);
        return result;
    }
    if (const auto* unknown = std::get_if<clangbridge::UnknownVersion>(&expr.node)) {
        auto value_type = convert_type(unknown->value_type);
        if (!value_type || unknown->operands.size() != 1)
            return std::nullopt;
        auto body = convert(unknown->operands.front());
        if (!body)
            return std::nullopt;
        result.node = vir::UnknownVersion{
            unknown->version, convert_place(unknown->place), *value_type, {std::move(*body)}, unknown->confined};
        return result;
    }

    if (const auto* bound = std::get_if<clangbridge::ElementBound>(&expr.node)) {
        if (bound->operands.size() != 2 || bound->extent.size() != 1)
            return std::nullopt;
        auto extent = convert(bound->extent.front());
        if (!extent)
            return std::nullopt;
        auto index = convert(bound->operands[0]);
        if (!index)
            return std::nullopt;
        auto body = convert(bound->operands[1]);
        if (!body)
            return std::nullopt;
        result.node = vir::ElementBound{{std::move(*extent)}, {std::move(*index), std::move(*body)}};
        return result;
    }

    if (const auto* parameter = std::get_if<clangbridge::ParameterRef>(&expr.node)) {
        result.node = vir::ParameterRef{parameter->index, parameter->name};
        return result;
    }

    if (const auto* literal = std::get_if<clangbridge::IntLiteral>(&expr.node)) {
        if (!type->is_integer() && !type->is_boolean() && !type->is_void()) {
            failure_ = Failure{"a literal of non-integer type is not modeled", expr.location};
            return std::nullopt;
        }
        result.node = vir::IntLiteral{literal->value};
        return result;
    }

    if (const auto* call = std::get_if<clangbridge::Call>(&expr.node)) {
        vir::Call converted;
        converted.callee = vir::SymbolId{call->callee_usr};
        converted.callee_name = call->callee_name;
        converted.library = call->library;
        converted.validation = call->validation;
        for (const clangbridge::Expr& argument : call->arguments) {
            std::optional<vir::Expr> converted_argument = convert(argument);
            if (!converted_argument.has_value()) {
                return std::nullopt;
            }
            converted.arguments.push_back(std::move(*converted_argument));
        }
        for (const auto& effect : call->effects) {
            auto declared = convert_type(effect.declared);
            if (!declared)
                return std::nullopt;
            converted.effects.push_back({effect.argument, effect.version, *declared});
        }
        result.node = std::move(converted);
        return result;
    }

    if (const auto* binary = std::get_if<clangbridge::Binary>(&expr.node)) {
        if (binary->op == clangbridge::BinaryOp::Unsupported || binary->operands.size() != 2) {
            failure_ = Failure{"this operator is not modeled", expr.location};
            return std::nullopt;
        }
        vir::Binary converted;
        converted.op = convert_operator(binary->op);
        for (const clangbridge::Expr& operand : binary->operands) {
            std::optional<vir::Expr> converted_operand = convert(operand);
            if (!converted_operand.has_value()) {
                return std::nullopt;
            }
            converted.operands.push_back(std::move(*converted_operand));
        }
        result.node = std::move(converted);
        return result;
    }

    if (const auto* negation = std::get_if<clangbridge::Negation>(&expr.node)) {
        vir::Negation converted;
        for (const auto& operand : negation->operands) {
            auto value = convert(operand);
            if (!value)
                return std::nullopt;
            converted.operands.push_back(std::move(*value));
        }
        result.node = std::move(converted);
        return result;
    }
    // Arithmetic negation and integral conversions keep the one operand the
    // bridge read; both are integers, which the lowering checks again
    // against the types it states them at (SPEC.md ARITH-006, ARITH-008).
    if (const auto* minus = std::get_if<clangbridge::Minus>(&expr.node)) {
        if (minus->operands.size() != 1 || !type->is_integer()) {
            failure_ = Failure{"malformed arithmetic negation", expr.location};
            return std::nullopt;
        }
        auto operand = convert(minus->operands.front());
        if (!operand)
            return std::nullopt;
        result.node = vir::Minus{{std::move(*operand)}};
        return result;
    }
    if (const auto* conversion = std::get_if<clangbridge::Conversion>(&expr.node)) {
        if (conversion->operands.size() != 1 || !type->is_integer()) {
            failure_ = Failure{"malformed integral conversion", expr.location};
            return std::nullopt;
        }
        auto operand = convert(conversion->operands.front());
        if (!operand)
            return std::nullopt;
        if (!operand->type.is_integer() && !operand->type.is_boolean()) {
            failure_ = Failure{"a conversion from '" + describe(operand->type) + "' is not modeled", expr.location};
            return std::nullopt;
        }
        result.node = vir::Conversion{{std::move(*operand)}, conversion->written};
        return result;
    }
    if (const auto* branch = std::get_if<clangbridge::Conditional>(&expr.node)) {
        vir::Conditional converted;
        for (const auto& operand : branch->operands) {
            auto value = convert(operand);
            if (!value)
                return std::nullopt;
            converted.operands.push_back(std::move(*value));
        }
        result.node = std::move(converted);
        return result;
    }

    if (const auto* bound = std::get_if<clangbridge::PlaceVersion>(&expr.node)) {
        vir::PlaceVersion converted;
        converted.version = bound->version;
        converted.place = convert_place(bound->place);
        if (const std::optional<vir::Type> declared = convert_type(bound->declared)) {
            converted.declared = *declared;
        }
        for (const auto& operand : bound->operands) {
            auto value = convert(operand);
            if (!value)
                return std::nullopt;
            converted.operands.push_back(std::move(*value));
        }
        result.node = std::move(converted);
        return result;
    }

    if (const auto* place = std::get_if<clangbridge::PlaceRef>(&expr.node)) {
        result.node = vir::PlaceRef{place->version, convert_place(place->place)};
        return result;
    }

    if (const auto* loop = std::get_if<clangbridge::Loop>(&expr.node)) {
        std::vector<vir::Place> places;
        places.reserve(loop->places.size());
        for (const auto& place : loop->places)
            places.push_back(convert_place(place));
        vir::Loop converted{loop->loop, loop->heads, std::move(places), 0, 0, {}};
        // One surface invariant can state a conjunction. Each conjunct is
        // still an independent entry/preservation obligation of the one
        // loop rule; no new fact is introduced and disjunction is not split.
        const auto append_invariant = [&](auto&& self, vir::Expr value) -> void {
            if (auto* conjunction = std::get_if<vir::Binary>(&value.node);
                conjunction && conjunction->op == vir::BinaryOp::And && conjunction->operands.size() == 2) {
                for (auto& operand : conjunction->operands)
                    self(self, std::move(operand));
            } else {
                ++converted.invariants;
                converted.operands.push_back(std::move(value));
            }
        };
        // The measure and the head follow the invariants, and splitting a
        // conjunction changes how many of those there are, so they are held
        // back and appended once every invariant is in place.
        std::vector<vir::Expr> trailing;
        for (std::size_t index = 0; index < loop->operands.size(); ++index) {
            auto value = convert(loop->operands[index]);
            if (!value)
                return std::nullopt;
            if (index >= loop->heads.size() && index < loop->heads.size() + loop->invariants)
                append_invariant(append_invariant, std::move(*value));
            else if (index < loop->heads.size())
                converted.operands.push_back(std::move(*value));
            else
                trailing.push_back(std::move(*value));
        }
        converted.measures = loop->measures;
        for (auto& value : trailing)
            converted.operands.push_back(std::move(value));
        result.node = std::move(converted);
        return result;
    }

    if (const auto* next = std::get_if<clangbridge::Iterate>(&expr.node)) {
        vir::Iterate converted{next->loop, {}};
        for (const auto& operand : next->operands) {
            auto value = convert(operand);
            if (!value)
                return std::nullopt;
            converted.operands.push_back(std::move(*value));
        }
        result.node = std::move(converted);
        return result;
    }

    // A claim that this path cannot occur (SPEC.md VERIFIED-023). Only a
    // verified body has one, so only its conversion is given the names.
    if (const auto* claim = std::get_if<clangbridge::PathContradiction>(&expr.node)) {
        const ResolvedClaim* resolved = nullptr;
        if (claims_ != nullptr) {
            if (const auto found = claims_->find(claim->marker); found != claims_->end()) {
                resolved = &found->second;
            }
        }
        if (resolved == nullptr) {
            failure_ = Failure{"a claim that a path cannot occur is written where this implementation does not "
                               "read one",
                               expr.location};
            return std::nullopt;
        }
        vir::PathContradiction converted{resolved->proof, resolved->evidence, {}, resolved->omitted};
        for (const auto& operand : claim->operands) {
            auto value = convert(operand);
            if (!value)
                return std::nullopt;
            converted.operands.push_back(std::move(*value));
        }
        result.node = std::move(converted);
        return result;
    }
    if (const auto* split = std::get_if<clangbridge::CaseSplit>(&expr.node)) {
        return convert_split(*split, expr, std::move(result));
    }
    // An unsafe block on this path (SPEC.md 26): no value and no fact, only
    // the rest of the path under the fresh versions the block leaves.
    if (const auto* region = std::get_if<clangbridge::UnsafeRegion>(&expr.node)) {
        if (region->operands.size() != 1) {
            failure_ = Failure{"malformed unsafe region", expr.location};
            return std::nullopt;
        }
        auto continued = convert(region->operands.front());
        if (!continued) {
            return std::nullopt;
        }
        result.node = vir::UnsafeRegion{{std::move(*continued)}};
        return result;
    }
    // A binder of an arm is the value its case exposes. The type Clang
    // resolved for its declaration must be that value's type, or what the
    // arm said about it was resolved at some other type.
    if (const auto* binder = std::get_if<clangbridge::CaseBinder>(&expr.node)) {
        const auto found = binders_.find(std::tuple{binder->marker, binder->arm, binder->index});
        if (found == binders_.end()) {
            failure_ = Failure{"a case binder is read where its arm does not stand", expr.location};
            return std::nullopt;
        }
        if (!(found->second.type == result.type)) {
            failure_ = Failure{"a case binder was resolved at type '" + describe(result.type) +
                                   "', but its case binds a value of type '" + describe(found->second.type) + "'",
                               expr.location};
            return std::nullopt;
        }
        vir::Expr value = found->second;
        value.provenance = result.provenance;
        return value;
    }
    // Every other node kind is refused by name rather than assumed to be
    // `Unsupported`. A kind added to the bridge with no handler here used
    // to reach `std::get` and throw `bad_variant_access` out of the
    // compiler: an unhandled expression form must fail closed, not by
    // exception (`AGENTS.md` 7, 23, 36).
    if (const auto* unsupported = std::get_if<clangbridge::Unsupported>(&expr.node)) {
        failure_ = Failure{unsupported->reason, expr.location};
        return std::nullopt;
    }
    failure_ = Failure{"this expression form has no formal meaning in this implementation", expr.location};
    return std::nullopt;
}

} // namespace cppl::elaboration
