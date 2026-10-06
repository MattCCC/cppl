// Lowering VIR types and values into core types and terms.

#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"
#include "generate_detail.hpp"
#include "lowering.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations {

using detail::generation::fail;
using detail::generation::lower_type;
using detail::generation::TermLowering;

namespace {

using detail::Failure;

} // namespace

namespace detail::generation {

std::unexpected<Failure> fail(std::string reason, const source::SourceLocation& location) {
    return std::unexpected(Failure{std::move(reason), location, {}});
}

std::optional<kernel::Type> lower_type(const vir::Type& type) {
    // C++ `bool` has exactly the two values the core's one-bit unsigned integer
    // has, and no arithmetic on it is modeled: every promotion to `int` is a
    // conversion the bridge already refuses (SPEC.md 29.2).
    if (type.is_boolean() || type.is_void()) {
        return kernel::Type{kernel::kBoolean};
    }
    if (type.is_value()) {
        std::vector<kernel::Type> projections;
        for (const auto& child : std::get<vir::ValueType>(type.node).projections) {
            auto lowered = lower_type(child);
            if (!lowered)
                return std::nullopt;
            projections.push_back(std::move(*lowered));
        }
        return kernel::Type::value(type.representation.identity, std::move(projections));
    }
    if (!type.is_integer()) {
        return std::nullopt;
    }
    const vir::IntType& integer = type.integer_type();
    return kernel::Type::integer(integer.width,
                                 integer.is_signed ? kernel::Signedness::Signed : kernel::Signedness::Unsigned);
}

} // namespace detail::generation

namespace {

// The indexed domain an array type denotes, for an observation whose index is a
// term (FOUNDATIONS.md 45).
//
// The extent comes from the resolved type, never from which elements happen to
// have been observed already: the type establishes the shape, and element facts
// only describe particular observations of it (ARCHITECTURE.md ARCH-ELEM-004).
// A homogeneous domain also requires that every element lower alike, so a
// signature whose components disagree is not one of these.
std::optional<kernel::Type> lower_indexed_type(const vir::Type& type) {
    // `std::array<T, N>` is observed at an index exactly as `T[N]` is
    // (SPEC.md STDMODEL-011).
    if (!type.is_value() || (type.representation.kind != source::RepresentationKind::Array &&
                             type.representation.kind != source::RepresentationKind::StdArray)) {
        return std::nullopt;
    }
    const auto& projections = std::get<vir::ValueType>(type.node).projections;
    if (projections.empty()) {
        return std::nullopt;
    }
    auto element = lower_type(projections.front());
    if (!element) {
        return std::nullopt;
    }
    for (const auto& child : projections) {
        auto lowered = lower_type(child);
        if (!lowered || !(*lowered == *element))
            return std::nullopt;
    }
    return kernel::Type::indexed(std::move(*element), static_cast<kernel::Wide>(projections.size()));
}

std::optional<kernel::PrimOp> comparison(vir::BinaryOp op) {
    switch (op) {
        case vir::BinaryOp::Equal:
            return kernel::PrimOp::Equal;
        case vir::BinaryOp::NotEqual:
            return kernel::PrimOp::NotEqual;
        case vir::BinaryOp::Less:
            return kernel::PrimOp::Less;
        case vir::BinaryOp::LessEqual:
            return kernel::PrimOp::LessEqual;
        case vir::BinaryOp::Greater:
            return kernel::PrimOp::Greater;
        case vir::BinaryOp::GreaterEqual:
            return kernel::PrimOp::GreaterEqual;
        case vir::BinaryOp::Add:
        case vir::BinaryOp::Sub:
        case vir::BinaryOp::Mul:
        case vir::BinaryOp::Div:
        case vir::BinaryOp::Rem:
        case vir::BinaryOp::And:
        case vir::BinaryOp::Or:
            return std::nullopt;
    }
    return std::nullopt;
}

// The total core primitive a C++ arithmetic operator is stated with, at the
// common type Clang gave the operation after the integral promotions and the
// usual arithmetic conversions. Where that type is unsigned, C++ defines `+`,
// `-` and `*` modulo 2^width exactly as the wrapping primitives do (SPEC.md
// ARITH-003). Where it is signed, and for `/` and `%` at any type, C++ defines
// the operator only under a condition, which every evaluation owes as an
// obligation of its own (definedness.cpp, SPEC.md ARITH-006, ARITH-007); once
// it is discharged, the linear rule proves the primitive's value is the exact
// C++ result. Stating a term never proves or assumes one.
std::optional<kernel::PrimOp> arithmetic(vir::BinaryOp op) {
    switch (op) {
        case vir::BinaryOp::Add:
            return kernel::PrimOp::AddWrap;
        case vir::BinaryOp::Sub:
            return kernel::PrimOp::SubWrap;
        case vir::BinaryOp::Mul:
            return kernel::PrimOp::MulWrap;
        case vir::BinaryOp::Div:
            return kernel::PrimOp::Quotient;
        case vir::BinaryOp::Rem:
            return kernel::PrimOp::Remainder;
        default:
            return std::nullopt;
    }
}

std::string operation(vir::BinaryOp op) {
    switch (op) {
        case vir::BinaryOp::Sub:
            return "subtraction";
        case vir::BinaryOp::Mul:
            return "multiplication";
        case vir::BinaryOp::Div:
            return "division";
        case vir::BinaryOp::Rem:
            return "remainder";
        default:
            return "addition";
    }
}

// A read of a local lowers its version's value again and the core has no
// sharing, so locals that each read the previous one twice double the term per
// statement. Past this bound the value is refused rather than expanded.
constexpr std::size_t kMaxTermNodes = std::size_t{1} << 14;

} // namespace

std::expected<kernel::Term, Failure> TermLowering::lower(const vir::Expr& expr) {
    const source::SourceLocation& location = expr.provenance.range.begin;

    // A local denotes the value its current version was given, so lowering
    // a version binds it and lowering a read replays that value here. The
    // value is a term, never a fresh unknown: nothing about the local is
    // assumed. The binding is scoped to the body under the version, so a
    // sibling arm's version cannot be read here even from malformed VIR.
    if (const auto* bound = std::get_if<vir::PlaceVersion>(&expr.node)) {
        if (bound->operands.size() != 2 || versions_.contains(bound->version)) {
            return fail("malformed local version", location);
        }
        // C++ evaluates the value where the local is written, whether or
        // not anything reads it afterwards, so it must be modeled there:
        // an unread signed overflow is still undefined behaviour.
        const std::uint32_t enclosing = replay_bound_;
        replay_bound_ = bound->version;
        auto evaluated = lower(bound->operands[0]);
        replay_bound_ = enclosing;
        if (!evaluated) {
            return evaluated;
        }
        versions_.emplace(bound->version, &bound->operands[0]);
        auto body = lower(bound->operands[1]);
        versions_.erase(bound->version);
        return body;
    }

    // A version's value reads only versions established before it, which
    // are numbered below it. Replaying under that bound makes a cycle
    // through malformed VIR a refusal rather than unbounded recursion.
    if (const auto* local = std::get_if<vir::PlaceRef>(&expr.node)) {
        // A loop's head version is a bound variable: what the path states
        // about it is all that is known.
        if (opaque_ != nullptr) {
            if (const auto head = opaque_->find(local->version); head != opaque_->end()) {
                if (head->second >= parameter_count_ || local->version >= replay_bound_) {
                    return fail("'" + describe(local->place) + "' is read outside the loop that gives it a value",
                                location);
                }
                return kernel::Term::variable(kernel::parameter_reference(parameter_count_, head->second));
            }
        }
        const auto version = versions_.find(local->version);
        if (version == versions_.end() || local->version >= replay_bound_) {
            return fail("'" + describe(local->place) + "' is read outside the path that gives it a value", location);
        }
        const std::uint32_t enclosing = replay_bound_;
        replay_bound_ = local->version;
        auto value = lower(*version->second);
        replay_bound_ = enclosing;
        return value;
    }

    if (++nodes_ > kMaxTermNodes) {
        return fail("this value expands to more than " + std::to_string(kMaxTermNodes) +
                        " core terms: each read of a local repeats the value it was given",
                    location);
    }
    const std::optional<kernel::Type> type = lower_type(expr.type);

    if (const auto* parameter = std::get_if<vir::ParameterRef>(&expr.node)) {
        if (parameter->parameter >= parameter_count_) {
            return fail("parameter reference is outside the declaration's parameter list", location);
        }
        return kernel::Term::variable(kernel::parameter_reference(parameter_count_, parameter->parameter));
    }

    if (const auto* projection = std::get_if<vir::Projection>(&expr.node)) {
        if (projection->operands.size() != 1)
            return fail("malformed logical projection", location);
        auto domain = lower_type(projection->operands[0].type);
        if (!domain || !domain->is_value())
            return fail("projection has no abstract domain", location);
        const auto& signature = std::get<kernel::ValueType>(domain->node).projections;
        if (!type || projection->index >= signature.size() || signature[projection->index] != *type)
            return fail("projection result disagrees with its domain signature", location);
        auto subject = lower(projection->operands[0]);
        if (!subject)
            return subject;
        return kernel::Term::project(*domain, projection->index, std::move(*subject));
    }

    if (const auto* element = std::get_if<vir::Element>(&expr.node)) {
        if (element->operands.size() != 2)
            return fail("malformed element observation", location);
        auto domain = lower_indexed_type(element->operands[0].type);
        if (!domain)
            return fail("this subscript's array has no modeled indexed structure", location);
        const auto& observed = std::get<kernel::IndexedType>(domain->node).element.front();
        if (!type || !(observed == *type))
            return fail("element result disagrees with its element type", location);
        auto subject = lower(element->operands[0]);
        if (!subject)
            return subject;
        auto index = lower(element->operands[1]);
        if (!index)
            return index;
        return kernel::Term::element(*domain, std::move(*subject), std::move(*index));
    }

    if (const auto* literal = std::get_if<vir::IntLiteral>(&expr.node)) {
        if (expr.type.is_void() && literal->value != 0)
            return fail("malformed void completion", location);
        if (!type.has_value()) {
            return fail("a literal of type '" + vir::describe(expr.type) + "' has no core representation", location);
        }
        if (!type->is_integer())
            return fail("literal requires an integer domain", location);
        const auto integer = type->integer_type();
        // A u64 literal above the signed range arrives with its bits in a
        // negative `int64_t`. The core denotes the value itself, so the
        // bits are read back as the unsigned value they stand for.
        if (integer.width == 64 && integer.signedness == kernel::Signedness::Unsigned && literal->value < 0) {
            return kernel::Term::literal(integer,
                                         static_cast<kernel::Wide>(static_cast<std::uint64_t>(literal->value)));
        }
        return kernel::Term::literal(integer, literal->value);
    }

    if (const auto* call = std::get_if<vir::Call>(&expr.node)) {
        if (calls_ != nullptr) {
            const auto binding = calls_->find(expr.id.value);
            if (binding != calls_->end()) {
                if (binding->second >= parameter_count_) {
                    return fail("call result is outside its logical scope", location);
                }
                return kernel::Term::variable(kernel::parameter_reference(parameter_count_, binding->second));
            }
        }
        const auto definition = definitions_.find(call->callee.usr);
        if (definition == definitions_.end()) {
            return std::unexpected(
                Failure{"'" + call->callee_name + "' is not available to the formal core as a definition", location,
                        call->callee.usr});
        }
        std::vector<kernel::Term> arguments;
        arguments.reserve(call->arguments.size());
        for (const vir::Expr& argument : call->arguments) {
            std::expected<kernel::Term, Failure> lowered = lower(argument);
            if (!lowered) {
                return lowered;
            }
            arguments.push_back(std::move(*lowered));
        }
        return kernel::Term::call(definition->second, std::move(arguments));
    }

    // A struct value assembled from its members is the fresh value the path that
    // evaluates it binds, and nothing anywhere else (TRUST.md TCB-AGGREGATE-001).
    if (std::holds_alternative<vir::Aggregate>(expr.node)) {
        if (calls_ != nullptr) {
            const auto binding = calls_->find(expr.id.value);
            if (binding != calls_->end()) {
                if (binding->second >= parameter_count_) {
                    return fail("an assembled struct value is outside its logical scope", location);
                }
                return kernel::Term::variable(kernel::parameter_reference(parameter_count_, binding->second));
            }
        }
        return fail("a struct value assembled from its members has no term outside a path that evaluates it: "
                    "the formal core builds no value from components, so such a value is verified path by path "
                    "and never unfolded",
                    location);
    }

    if (const auto* binary = std::get_if<vir::Binary>(&expr.node)) {
        // As a value, `a && b` is `a ? b : false` and `a || b` is `a ? true : b`.
        if (binary->op == vir::BinaryOp::And || binary->op == vir::BinaryOp::Or) {
            auto lhs = binary->operands.size() == 2 ? lower(binary->operands[0]) : fail("malformed", location);
            auto rhs = lhs ? lower(binary->operands[1]) : lhs;
            if (!rhs || !type || !expr.type.is_boolean())
                return rhs ? fail("malformed connective", location) : rhs;
            const bool both = binary->op == vir::BinaryOp::And;
            const auto fixed = kernel::Term::literal(type->integer_type(), both ? 0 : 1);
            return kernel::Term::primitive(kernel::PrimOp::Select, type->integer_type(),
                                           {*lhs, both ? *rhs : fixed, both ? fixed : *rhs});
        }
        if (const auto op = comparison(binary->op)) {
            if (binary->operands.size() != 2 || !expr.type.is_boolean()) {
                return fail("malformed comparison", location);
            }
            const auto left = lower_type(binary->operands[0].type);
            const auto right = lower_type(binary->operands[1].type);
            // Two abstract values, pointers among them, have no order and
            // no machine equality the core states: `==` on them is refused,
            // never lowered as an integer comparison of something else.
            if (!left || !right || !(*left == *right) || !left->is_integer()) {
                return fail("comparison requires equal-typed modeled integers", location);
            }
            auto lhs = lower(binary->operands[0]);
            auto rhs = lower(binary->operands[1]);
            if (!lhs || !rhs)
                return std::unexpected(!lhs ? lhs.error() : rhs.error());
            return kernel::Term::primitive(*op, left->integer_type(), {std::move(*lhs), std::move(*rhs)});
        }
        const std::optional<kernel::PrimOp> primitive = arithmetic(binary->op);
        if (!primitive.has_value() || binary->operands.size() != 2) {
            return fail("'" + vir::describe(binary->op) + "' does not denote a value in the formal core", location);
        }
        if (!type.has_value() || !type->is_integer() || expr.type.is_boolean()) {
            return fail(operation(binary->op) + " at type '" + vir::describe(expr.type) + "' is not modeled", location);
        }

        const kernel::IntType integer = type->integer_type();
        std::vector<kernel::Term> operands;
        operands.reserve(binary->operands.size());
        for (const vir::Expr& operand : binary->operands) {
            // C++ converted each operand to the operation's type, and each
            // conversion Clang recorded is a node of its own, so what is
            // left is that machine type, whatever it was spelled.
            if (lower_type(operand.type) != type || operand.type.is_boolean()) {
                return fail("arithmetic requires operands of its own modeled type", location);
            }
            std::expected<kernel::Term, Failure> lowered = lower(operand);
            if (!lowered) {
                return lowered;
            }
            operands.push_back(std::move(*lowered));
        }
        return kernel::Term::primitive(*primitive, integer, std::move(operands));
    }

    if (const auto* negation = std::get_if<vir::Negation>(&expr.node)) {
        if (negation->operands.size() != 1 || !negation->operands[0].type.is_boolean()) {
            return fail("negation requires a comparison predicate", location);
        }
        auto operand = lower(negation->operands[0]);
        if (!operand)
            return operand;
        return kernel::Term::primitive(kernel::PrimOp::Not, kernel::kBoolean, {std::move(*operand)});
    }
    // `-x` is `0 - x` at the promoted type: modular for an unsigned type,
    // and for a signed one owing the same representability a subtraction
    // from zero owes (SPEC.md ARITH-006).
    if (const auto* minus = std::get_if<vir::Minus>(&expr.node)) {
        if (minus->operands.size() != 1 || !type || !type->is_integer() || expr.type.is_boolean() ||
            lower_type(minus->operands[0].type) != type) {
            return fail("arithmetic negation requires an integer operand of its own type", location);
        }
        auto operand = lower(minus->operands[0]);
        if (!operand)
            return operand;
        return kernel::Term::primitive(kernel::PrimOp::SubWrap, type->integer_type(),
                                       {kernel::Term::literal(type->integer_type(), 0), std::move(*operand)});
    }
    // An integral conversion Clang recorded, stated as two's-complement
    // reduction into the target type. That is C++'s conversion to an
    // unsigned type. To a signed type, every evaluation owes that the value
    // is representable in the target, and where it is the reduction's value
    // is proven to be that value unchanged (SPEC.md ARITH-008). `bool` is
    // the core's one-bit unsigned type, so its conversion to an integer type
    // is this reduction of 0 or 1, which is C++'s. A conversion to `bool`
    // is whether the value is nonzero and is never stated this way.
    if (const auto* conversion = std::get_if<vir::Conversion>(&expr.node)) {
        if (conversion->operands.size() != 1 || !type || !type->is_integer() || expr.type.is_boolean()) {
            return fail("an integral conversion requires integer operand and result types", location);
        }
        const std::optional<kernel::Type> source = lower_type(conversion->operands[0].type);
        if (!source || !source->is_integer()) {
            return fail("a conversion from '" + vir::describe(conversion->operands[0].type) +
                            "' has no core representation",
                        location);
        }
        auto operand = lower(conversion->operands[0]);
        if (!operand)
            return operand;
        return kernel::Term::primitive(kernel::PrimOp::Convert, type->integer_type(), {std::move(*operand)});
    }
    if (const auto* branch = std::get_if<vir::Conditional>(&expr.node)) {
        if (branch->operands.size() != 3 || !type || !type->is_integer() || !branch->operands[0].type.is_boolean() ||
            !(branch->operands[1].type == expr.type) || !(branch->operands[2].type == expr.type)) {
            return fail("conditional requires a comparison and equal-typed integer returns", location);
        }
        std::vector<kernel::Term> operands;
        for (const auto& operand : branch->operands) {
            auto value = lower(operand);
            if (!value)
                return value;
            operands.push_back(std::move(*value));
        }
        return kernel::Term::primitive(kernel::PrimOp::Select, type->integer_type(), std::move(operands));
    }

    // A bound states an obligation about the index; it does not change the
    // value the body denotes. The obligation itself is emitted where the
    // path is walked, so lowering here is lowering the body.
    if (const auto* bounded = std::get_if<vir::ElementBound>(&expr.node)) {
        if (bounded->operands.size() != 2 || bounded->extent.size() != 1) {
            return fail("malformed element bound", location);
        }
        return lower(bounded->operands[1]);
    }

    if (std::holds_alternative<vir::Loop>(expr.node) || std::holds_alternative<vir::Iterate>(expr.node)) {
        return fail("a loop has no total core term: what it computes is established by partial-correctness "
                    "obligations, never unfolded",
                    location);
    }

    if (std::holds_alternative<vir::PathContradiction>(expr.node)) {
        return fail("a path claimed not to occur has no value: the claim is an obligation of its own, never a "
                    "term",
                    location);
    }

    if (std::holds_alternative<vir::CaseSplit>(expr.node)) {
        return fail("a case split has no value: each of its arms is a path of its own, walked as one", location);
    }

    if (std::holds_alternative<vir::UnsafeRegion>(expr.node)) {
        return fail("an unsafe block has no core term: what it does is not modeled, so a body crossing one is "
                    "verified path by path and never unfolded",
                    location);
    }

    return fail("this expression has no core representation", location);
}

} // namespace cppl::obligations
