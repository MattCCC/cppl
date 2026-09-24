#include "cppl/obligations/generate.hpp"

#include "contradiction.hpp"
#include "cppl/decomposition/decomposition.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/kernel/version.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/vir/capability.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations {

namespace {

using detail::arithmetic_fact;
using detail::Failure;
using detail::Standing;
using detail::Unestablished;

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

// The indexed domain an array type denotes, for an observation whose index is a
// term (FOUNDATIONS.md 45).
//
// The extent comes from the resolved type, never from which elements happen to
// have been observed already: the type establishes the shape, and element facts
// only describe particular observations of it (ARCHITECTURE.md ARCH-ELEM-004).
// A homogeneous domain also requires that every element lower alike, so a
// signature whose components disagree is not one of these.
std::optional<kernel::Type> lower_indexed_type(const vir::Type& type) {
    if (!type.is_value() || type.representation.kind != source::RepresentationKind::Array) {
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

using detail::DefinitionMap;

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
        case vir::BinaryOp::And:
        case vir::BinaryOp::Or:
            return std::nullopt;
    }
    return std::nullopt;
}

// The wrapping primitive a C++ arithmetic operator denotes on unsigned
// operands, where C++ defines the result modulo 2^width exactly as the core
// primitive does (SPEC.md 29.2).
std::optional<kernel::PrimOp> modular(vir::BinaryOp op) {
    switch (op) {
        case vir::BinaryOp::Add:
            return kernel::PrimOp::AddWrap;
        case vir::BinaryOp::Sub:
            return kernel::PrimOp::SubWrap;
        case vir::BinaryOp::Mul:
            return kernel::PrimOp::MulWrap;
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
        default:
            return "addition";
    }
}

// A read of a local lowers its version's value again and the core has no
// sharing, so locals that each read the previous one twice double the term per
// statement. Past this bound the value is refused rather than expanded.
constexpr std::size_t kMaxTermNodes = std::size_t{1} << 14;

// Lowers a VIR value expression into a core term.
//
// The core is total: every primitive it offers is defined on every input. A C++
// operator may be lowered onto one only when the C++ operator is equally total.
// Where it is not, the lowering refuses rather than pretending (SPEC.md 29, 31).
class TermLowering {
  public:
    TermLowering(const DefinitionMap& definitions, std::size_t parameter_count,
                 const detail::CallBindings* calls = nullptr, const detail::VersionBindings* versions = nullptr,
                 const detail::OpaqueBindings* opaque = nullptr)
        : definitions_(definitions),
          parameter_count_(parameter_count),
          calls_(calls),
          versions_(versions != nullptr ? *versions : detail::VersionBindings{}),
          opaque_(opaque) {}

    [[nodiscard]] std::expected<kernel::Term, Failure> lower(const vir::Expr& expr) {
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
                return fail("'" + describe(local->place) + "' is read outside the path that gives it a value",
                            location);
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
                return fail("a literal of type '" + vir::describe(expr.type) + "' has no core representation",
                            location);
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

        if (const auto* binary = std::get_if<vir::Binary>(&expr.node)) {
            // `&&` and `||` state a proposition, and a proposition is not a
            // value: nothing computes one. Where a value is required - a
            // returned expression, a condition a path is taken on - they are
            // refused rather than encoded as a Boolean.
            if (binary->op == vir::BinaryOp::And || binary->op == vir::BinaryOp::Or) {
                return fail("'" + vir::describe(binary->op) +
                                "' states a proposition and is not modeled as a value: this position requires a "
                                "value, such as a condition a path is taken on or a loop invariant" +
                                (binary->op == vir::BinaryOp::And ? ", so state each side separately" : ""),
                            location);
            }
            if (const auto op = comparison(binary->op)) {
                if (binary->operands.size() != 2 || !expr.type.is_boolean()) {
                    return fail("malformed comparison", location);
                }
                const auto left = lower_type(binary->operands[0].type);
                const auto right = lower_type(binary->operands[1].type);
                if (!left || !right || !(*left == *right)) {
                    return fail("comparison requires equal-typed modeled integers", location);
                }
                auto lhs = lower(binary->operands[0]);
                auto rhs = lower(binary->operands[1]);
                if (!lhs || !rhs)
                    return std::unexpected(!lhs ? lhs.error() : rhs.error());
                return kernel::Term::primitive(*op, left->integer_type(), {std::move(*lhs), std::move(*rhs)});
            }
            const std::optional<kernel::PrimOp> primitive = modular(binary->op);
            if (!primitive.has_value() || binary->operands.size() != 2) {
                return fail("'" + vir::describe(binary->op) + "' does not denote a value in the formal core", location);
            }
            if (!type.has_value() || expr.type.is_boolean()) {
                return fail(operation(binary->op) + " at type '" + vir::describe(expr.type) + "' is not modeled",
                            location);
            }

            const kernel::IntType integer = type->integer_type();
            if (integer.signedness == kernel::Signedness::Signed) {
                // Unsigned C++ arithmetic is modular and matches the core's
                // wrapping primitives exactly. Signed C++ arithmetic has
                // undefined behaviour on overflow, so it is not those
                // primitives, and the obligation that would justify the
                // difference is not part of the core yet.
                return fail(operation(binary->op) + " on the signed type '" + vir::describe(expr.type) +
                                "' is not modeled: C++ leaves signed overflow undefined, and this "
                                "implementation cannot yet discharge the obligation that it does not "
                                "occur",
                            location);
            }

            std::vector<kernel::Term> operands;
            operands.reserve(binary->operands.size());
            for (const vir::Expr& operand : binary->operands) {
                if (!(operand.type == expr.type)) {
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
        if (const auto* branch = std::get_if<vir::Conditional>(&expr.node)) {
            if (branch->operands.size() != 3 || !type || !branch->operands[0].type.is_boolean() ||
                !(branch->operands[1].type == expr.type) || !(branch->operands[2].type == expr.type)) {
                return fail("conditional requires a comparison and equal-typed returns", location);
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

  private:
    const DefinitionMap& definitions_;
    std::size_t parameter_count_;
    const detail::CallBindings* calls_;
    detail::VersionBindings versions_;
    const detail::OpaqueBindings* opaque_;
    std::uint32_t replay_bound_ = std::numeric_limits<std::uint32_t>::max();
    std::size_t nodes_ = 0;
};

// Lowers a specification expression into a core proposition.
//
// A C++ equality between built-in integer values denotes propositional equality
// of those values. The correspondence holds for this operand type only, and is
// established here rather than assumed anywhere else (SPEC.md 7.3).
// Equivalence duplicates both operands in its derived core representation.
// Bound that expansion before allocating it; source nesting alone is not a
// useful bound for repeated equivalences.
bool fits_proposition(const vir::Expr& expression, std::size_t copies, std::size_t& remaining, unsigned depth = 0) {
    if (depth > 128 || copies > remaining)
        return false;
    remaining -= copies;
    return std::visit(
        [&](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, vir::Connective>) {
                if (node.kind == vir::Connective::Kind::Equivalence) {
                    if (copies > remaining / 2)
                        return false;
                    remaining -= 2 * copies; // And plus two Implies nodes
                    copies *= 2;
                }
            }
            if constexpr (requires { node.operands; }) {
                for (const auto& operand : node.operands)
                    if (!fits_proposition(operand, copies, remaining, depth + 1))
                        return false;
            } else if constexpr (requires { node.arguments; }) {
                for (const auto& argument : node.arguments)
                    if (!fits_proposition(argument, copies, remaining, depth + 1))
                        return false;
            } else if constexpr (std::is_same_v<Node, vir::Universal>) {
                if (node.binders.size() > remaining / copies)
                    return false;
                remaining -= node.binders.size() * copies;
                for (const auto& body : node.body)
                    if (!fits_proposition(body, copies, remaining, depth + 1))
                        return false;
            }
            return true;
        },
        expression.node);
}

std::expected<kernel::Proposition, Failure> lower_proposition(const vir::Expr& expression,
                                                              const DefinitionMap& definitions,
                                                              std::size_t parameter_count) {
    const source::SourceLocation& location = expression.provenance.range.begin;
    std::size_t expansion_budget = 16384;
    if (!fits_proposition(expression, 1, expansion_budget))
        return fail("logical proposition expansion exceeds the supported limit", location);

    if (const auto* quantified = std::get_if<vir::Universal>(&expression.node)) {
        if (!expression.type.is_proposition() || quantified->binders.empty() || quantified->body.size() != 1)
            return fail("malformed universal proposition", location);
        auto body = lower_proposition(quantified->body[0], definitions, parameter_count + quantified->binders.size());
        if (!body)
            return body;
        for (const auto& binder : std::views::reverse(quantified->binders)) {
            const auto type = lower_type(binder);
            if (!type)
                return fail("unsupported quantifier binder type", location);
            *body = kernel::Proposition::for_all(*type, std::move(*body));
        }
        return body;
    }
    // C++ `&&` between Boolean operands states the conjunction of what those
    // operands state (SPEC.md 7.6). Each operand is a
    // side-effect-free specification expression, so short-circuiting changes
    // which of them C++ evaluates and never what the statement means.
    // `||` states the disjunction of its operands for the same reason (SPEC.md
    // 7.8): both are pure specification expressions, so which of them C++ would
    // evaluate does not enter into what the proposition says.
    if (const auto* binary = std::get_if<vir::Binary>(&expression.node);
        binary != nullptr && (binary->op == vir::BinaryOp::And || binary->op == vir::BinaryOp::Or)) {
        const bool conjunction = binary->op == vir::BinaryOp::And;
        if (!expression.type.is_boolean() || binary->operands.size() != 2 || !binary->operands[0].type.is_boolean() ||
            !binary->operands[1].type.is_boolean())
            return fail(conjunction ? "malformed conjunction" : "malformed disjunction", location);
        auto left = lower_proposition(binary->operands[0], definitions, parameter_count);
        if (!left)
            return left;
        auto right = lower_proposition(binary->operands[1], definitions, parameter_count);
        if (!right)
            return right;
        return conjunction ? kernel::Proposition::conjunction(std::move(*left), std::move(*right))
                           : kernel::Proposition::disjunction(std::move(*left), std::move(*right));
    }

    if (const auto* connective = std::get_if<vir::Connective>(&expression.node)) {
        if (!expression.type.is_proposition() || connective->operands.size() != 2)
            return fail("malformed logical connective", location);
        auto left = lower_proposition(connective->operands[0], definitions, parameter_count);
        if (!left)
            return left;
        auto right = lower_proposition(connective->operands[1], definitions, parameter_count);
        if (!right)
            return right;
        switch (connective->kind) {
            case vir::Connective::Kind::Conjunction:
                return kernel::Proposition::conjunction(std::move(*left), std::move(*right));
            case vir::Connective::Kind::Disjunction:
                return kernel::Proposition::disjunction(std::move(*left), std::move(*right));
            case vir::Connective::Kind::Equivalence:
                return kernel::Proposition::conjunction(kernel::Proposition::implication(*left, *right),
                                                        kernel::Proposition::implication(*right, *left));
        }
        return fail("unknown logical connective", location);
    }

    if (const auto* implication = std::get_if<vir::Implication>(&expression.node)) {
        if (!expression.type.is_proposition() || implication->operands.size() != 2)
            return fail("malformed implication proposition", location);
        auto premise = lower_proposition(implication->operands[0], definitions, parameter_count);
        if (!premise)
            return premise;
        auto conclusion = lower_proposition(implication->operands[1], definitions, parameter_count);
        if (!conclusion)
            return conclusion;
        return kernel::Proposition::implication(std::move(*premise), std::move(*conclusion));
    }

    if (const auto* equality = std::get_if<vir::FormalEquality>(&expression.node)) {
        const auto type = lower_type(equality->operand_type);
        if (!expression.type.is_proposition() || !type || equality->operands.size() != 2 ||
            !(equality->operands[0].type == equality->operand_type) ||
            !(equality->operands[1].type == equality->operand_type)) {
            return fail("malformed formal equality", location);
        }
        TermLowering lowering(definitions, parameter_count);
        auto lhs = lowering.lower(equality->operands[0]);
        if (!lhs)
            return std::unexpected(lhs.error());
        auto rhs = lowering.lower(equality->operands[1]);
        if (!rhs)
            return std::unexpected(rhs.error());
        return kernel::Proposition::equality(*type, std::move(*lhs), std::move(*rhs));
    }

    if (!expression.type.is_boolean())
        return fail("it does not state a comparison", location);
    TermLowering lowering(definitions, parameter_count);
    auto condition = lowering.lower(expression);
    if (!condition)
        return std::unexpected(condition.error());
    return kernel::predicate(*condition, true);
}

// Closes a proposition over a declaration's parameters, outermost first.
std::optional<kernel::Proposition> quantify_over(const std::vector<vir::Parameter>& parameters,
                                                 kernel::Proposition body, std::string& unrepresented) {
    for (const auto& parameter : std::views::reverse(parameters)) {
        const std::optional<kernel::Type> binder = lower_type(parameter.type);
        if (!binder.has_value()) {
            unrepresented = vir::describe(parameter.type);
            return std::nullopt;
        }
        body = kernel::Proposition::for_all(*binder, std::move(body));
    }
    return body;
}

void collect_callees(const vir::Expr& expr, std::set<std::string>& callees) {
    if (const auto* call = std::get_if<vir::Call>(&expr.node)) {
        callees.insert(call->callee.usr);
        for (const vir::Expr& argument : call->arguments) {
            collect_callees(argument, callees);
        }
        return;
    }
    if (const auto* binary = std::get_if<vir::Binary>(&expr.node)) {
        for (const vir::Expr& operand : binary->operands) {
            collect_callees(operand, callees);
        }
    }
    if (const auto* branch = std::get_if<vir::Conditional>(&expr.node)) {
        for (const auto& operand : branch->operands)
            collect_callees(operand, callees);
    }
    if (const auto* bound = std::get_if<vir::PlaceVersion>(&expr.node)) {
        for (const auto& operand : bound->operands)
            collect_callees(operand, callees);
    }
    if (const auto* negation = std::get_if<vir::Negation>(&expr.node)) {
        for (const auto& operand : negation->operands)
            collect_callees(operand, callees);
    }
    if (const auto* loop = std::get_if<vir::Loop>(&expr.node)) {
        for (const auto& operand : loop->operands)
            collect_callees(operand, callees);
    }
    if (const auto* next = std::get_if<vir::Iterate>(&expr.node)) {
        for (const auto& operand : next->operands)
            collect_callees(operand, callees);
    }
}

void encode(source::Hasher& hasher, const kernel::Type& type);
void encode(source::Hasher& hasher, const kernel::Term& term);
void encode(source::Hasher& hasher, const kernel::Proposition& proposition);

void encode(source::Hasher& hasher, const kernel::Type& type) {
    if (type.is_value()) {
        hasher.update_u8(2);
        const auto& value = std::get<kernel::ValueType>(type.node);
        hasher.update_field(value.identity);
        hasher.update_u64(value.projections.size());
        for (const auto& projection : value.projections)
            encode(hasher, projection);
        return;
    }
    if (type.is_indexed()) {
        // The extent is part of type identity, so it is hashed: a domain of 4
        // and a domain of 8 must not share an identity.
        hasher.update_u8(3);
        const auto& indexed = std::get<kernel::IndexedType>(type.node);
        const auto bits = static_cast<kernel::WideUnsigned>(indexed.extent);
        hasher.update_u64(static_cast<std::uint64_t>(bits));
        hasher.update_u64(static_cast<std::uint64_t>(bits >> 64));
        for (const auto& element : indexed.element)
            encode(hasher, element);
        return;
    }
    hasher.update_u8(1);
    const kernel::IntType& integer = type.integer_type();
    hasher.update_u64(integer.width);
    hasher.update_u8(integer.signedness == kernel::Signedness::Signed ? 1 : 0);
}

void encode(source::Hasher& hasher, const kernel::Term& term) {
    std::visit(
        [&hasher](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, kernel::Var>) {
                hasher.update_u8(10);
                hasher.update_u64(node.index.value);
            } else if constexpr (std::is_same_v<Node, kernel::Literal>) {
                hasher.update_u8(11);
                encode(hasher, kernel::Type{node.type});
                // Both halves: a literal's value is 128 bits wide, so hashing
                // only the low half would give two distinct literals one
                // identity.
                const auto bits = static_cast<kernel::WideUnsigned>(node.value);
                hasher.update_u64(static_cast<std::uint64_t>(bits));
                hasher.update_u64(static_cast<std::uint64_t>(bits >> 64));
            } else if constexpr (std::is_same_v<Node, kernel::Call>) {
                hasher.update_u8(12);
                hasher.update_u64(node.callee.value);
                hasher.update_u64(node.arguments.size());
                for (const kernel::Term& argument : node.arguments) {
                    encode(hasher, argument);
                }
            } else if constexpr (std::is_same_v<Node, kernel::Projection>) {
                hasher.update_u8(14);
                encode(hasher, node.domain);
                hasher.update_u64(node.index);
                hasher.update_u64(node.arguments.size());
                for (const auto& argument : node.arguments)
                    encode(hasher, argument);
            } else if constexpr (std::is_same_v<Node, kernel::Element>) {
                // A distinct tag, and the index hashed as the argument it is:
                // observations at different index terms must not share one
                // obligation identity.
                hasher.update_u8(15);
                encode(hasher, node.domain);
                hasher.update_u64(node.arguments.size());
                for (const auto& argument : node.arguments)
                    encode(hasher, argument);
            } else {
                hasher.update_u8(13);
                hasher.update_u8(static_cast<std::uint8_t>(node.op));
                encode(hasher, kernel::Type{node.type});
                hasher.update_u64(node.arguments.size());
                for (const kernel::Term& argument : node.arguments) {
                    encode(hasher, argument);
                }
            }
        },
        term.node);
}

void encode(source::Hasher& hasher, const kernel::Proposition& proposition) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&proposition.node)) {
        hasher.update_u8(20);
        encode(hasher, quantified->binder);
        encode(hasher, *quantified->body);
        return;
    }
    if (const auto* implication = std::get_if<kernel::Implies>(&proposition.node)) {
        hasher.update_u8(22);
        encode(hasher, *implication->premise);
        encode(hasher, *implication->conclusion);
        return;
    }
    if (const auto* conjunction = std::get_if<kernel::And>(&proposition.node)) {
        hasher.update_u8(23);
        encode(hasher, *conjunction->left);
        encode(hasher, *conjunction->right);
        return;
    }
    if (const auto* disjunction = std::get_if<kernel::Or>(&proposition.node)) {
        hasher.update_u8(24);
        encode(hasher, *disjunction->left);
        encode(hasher, *disjunction->right);
        return;
    }
    if (std::holds_alternative<kernel::Falsity>(proposition.node)) {
        hasher.update_u8(25);
        return;
    }
    const auto& equality = std::get<kernel::Eq>(proposition.node);
    hasher.update_u8(21);
    encode(hasher, equality.type);
    encode(hasher, equality.lhs);
    encode(hasher, equality.rhs);
}

void encode(source::Hasher& hasher, const kernel::Definition& definition) {
    hasher.update_u8(30);
    hasher.update_u64(definition.id.value);
    hasher.update_u64(definition.parameters.size());
    for (const kernel::Type& parameter : definition.parameters) {
        encode(hasher, parameter);
    }
    encode(hasher, definition.result);
    encode(hasher, definition.body);
}

void collect_dependencies(const kernel::Context& context, const kernel::Term& term, std::set<std::uint32_t>& reached);

void collect_dependencies(const kernel::Context& context, const kernel::Proposition& proposition,
                          std::set<std::uint32_t>& reached) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&proposition.node)) {
        collect_dependencies(context, *quantified->body, reached);
        return;
    }
    if (const auto* implication = std::get_if<kernel::Implies>(&proposition.node)) {
        collect_dependencies(context, *implication->premise, reached);
        collect_dependencies(context, *implication->conclusion, reached);
        return;
    }
    if (const auto* conjunction = std::get_if<kernel::And>(&proposition.node)) {
        collect_dependencies(context, *conjunction->left, reached);
        collect_dependencies(context, *conjunction->right, reached);
        return;
    }
    if (const auto* disjunction = std::get_if<kernel::Or>(&proposition.node)) {
        collect_dependencies(context, *disjunction->left, reached);
        collect_dependencies(context, *disjunction->right, reached);
        return;
    }
    if (std::holds_alternative<kernel::Falsity>(proposition.node)) {
        return;
    }
    const auto& equality = std::get<kernel::Eq>(proposition.node);
    collect_dependencies(context, equality.lhs, reached);
    collect_dependencies(context, equality.rhs, reached);
}

void collect_dependencies(const kernel::Context& context, const kernel::Term& term, std::set<std::uint32_t>& reached) {
    if (const auto* call = std::get_if<kernel::Call>(&term.node)) {
        if (reached.insert(call->callee.value).second) {
            if (const kernel::Definition* definition = context.lookup(call->callee)) {
                collect_dependencies(context, definition->body, reached);
            }
        }
        for (const kernel::Term& argument : call->arguments) {
            collect_dependencies(context, argument, reached);
        }
        return;
    }
    std::visit(
        [&](const auto& node) {
            if constexpr (requires { node.arguments; })
                for (const auto& argument : node.arguments)
                    collect_dependencies(context, argument, reached);
        },
        term.node);
}

// The identity of an obligation is the content it depends on: the formal core
// and kernel versions, the law it comes from, the goal, and the definitions the
// goal can actually reach. Unrelated definitions elsewhere in the unit do not
// change it.
ObligationId identify(const kernel::Context& context, const std::string& law_name, const kernel::Proposition& goal) {
    std::set<std::uint32_t> reached;
    collect_dependencies(context, goal, reached);

    source::Hasher hasher;
    hasher.update_field(kernel::kFormalCoreVersion);
    hasher.update_field(kernel::kKernelVersion);
    hasher.update_field(law_name);
    hasher.update_u64(reached.size());
    for (std::uint32_t id : reached) {
        if (const kernel::Definition* definition = context.lookup(kernel::DefId{id})) {
            encode(hasher, *definition);
        }
    }
    encode(hasher, goal);
    return ObligationId{hasher.finish()};
}

// Whether evidence for `available` can stand as evidence for `goal`.
//
// The quantifier prefix and the type of the equality must agree: a proof term
// restates its binders, so a term built for one prefix is not a term for
// another. Whether the two equalities themselves coincide is left to the
// kernel, which is the point of `apply`.
bool conclusion_is_applicable(const kernel::Proposition& available, const kernel::Proposition& goal,
                              std::string& reason) {
    const auto* available_forall = std::get_if<kernel::Forall>(&available.node);
    const auto* goal_forall = std::get_if<kernel::Forall>(&goal.node);

    if (available_forall != nullptr && goal_forall != nullptr) {
        if (!(available_forall->binder == goal_forall->binder)) {
            reason = "it quantifies over '" + kernel::describe(available_forall->binder) +
                     "' where the goal quantifies over '" + kernel::describe(goal_forall->binder) + "'";
            return false;
        }
        return conclusion_is_applicable(*available_forall->body, *goal_forall->body, reason);
    }

    if (available_forall != nullptr || goal_forall != nullptr) {
        reason = "it quantifies over a different number of variables than the goal";
        return false;
    }

    const auto* available_implies = std::get_if<kernel::Implies>(&available.node);
    const auto* goal_implies = std::get_if<kernel::Implies>(&goal.node);

    if (available_implies != nullptr && goal_implies != nullptr) {
        return conclusion_is_applicable(*available_implies->premise, *goal_implies->premise, reason) &&
               conclusion_is_applicable(*available_implies->conclusion, *goal_implies->conclusion, reason);
    }

    if (available_implies != nullptr || goal_implies != nullptr) {
        reason = available_implies != nullptr ? "it supposes a premise the goal does not"
                                              : "the goal supposes a premise it does not";
        return false;
    }

    const auto* available_and = std::get_if<kernel::And>(&available.node);
    const auto* goal_and = std::get_if<kernel::And>(&goal.node);

    if (available_and != nullptr && goal_and != nullptr) {
        return conclusion_is_applicable(*available_and->left, *goal_and->left, reason) &&
               conclusion_is_applicable(*available_and->right, *goal_and->right, reason);
    }

    if (available_and != nullptr || goal_and != nullptr) {
        reason = available_and != nullptr ? "it states a conjunction the goal does not"
                                          : "the goal states a conjunction it does not";
        return false;
    }

    const auto* available_or = std::get_if<kernel::Or>(&available.node);
    const auto* goal_or = std::get_if<kernel::Or>(&goal.node);

    if (available_or != nullptr && goal_or != nullptr) {
        return conclusion_is_applicable(*available_or->left, *goal_or->left, reason) &&
               conclusion_is_applicable(*available_or->right, *goal_or->right, reason);
    }

    if (available_or != nullptr || goal_or != nullptr) {
        reason = available_or != nullptr ? "it states a disjunction the goal does not"
                                         : "the goal states a disjunction it does not";
        return false;
    }

    const bool available_false = std::holds_alternative<kernel::Falsity>(available.node);
    const bool goal_false = std::holds_alternative<kernel::Falsity>(goal.node);
    if (available_false || goal_false) {
        if (available_false != goal_false) {
            reason = available_false ? "it concludes False where the goal is an equality"
                                     : "the goal is False and it concludes an equality";
        }
        return available_false == goal_false;
    }

    const auto& available_equality = std::get<kernel::Eq>(available.node);
    const auto& goal_equality = std::get<kernel::Eq>(goal.node);
    if (!(available_equality.type == goal_equality.type)) {
        reason = "it is an equality at '" + kernel::describe(available_equality.type) +
                 "' where the goal is an equality at '" + kernel::describe(goal_equality.type) + "'";
        return false;
    }
    return true;
}

void report(diagnostics::Engine& engine, diagnostics::Category category, const source::SourceLocation& location,
            std::string message, std::string note = {}) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = category;
    diagnostic.message = std::move(message);
    diagnostic.location = location;
    if (!note.empty()) {
        diagnostic.notes.push_back(diagnostics::Note{std::move(note), location});
    }
    engine.report(std::move(diagnostic));
}

// The proposition a `proves` clause claims.
//
// A law states what holds for every inhabitant of its parameters. Naming it at
// particular arguments claims one instance of it, which is that statement with
// the quantifiers instantiated: universal elimination, performed here on the
// proposition by the kernel's own substitution. What remains open are the
// proof's own parameters, and those are quantified back over the result.
std::optional<kernel::Proposition> claimed_proposition(const vir::Proof& proof, const Obligation& obligation,
                                                       const DefinitionMap& definitions, diagnostics::Engine& engine) {
    if (!proof.law)
        return obligation.goal;
    const auto& claim = std::get<vir::Call>(proof.proposition.node);
    TermLowering lowering(definitions, proof.parameters.size());

    kernel::Proposition instance = obligation.goal;
    for (const vir::Expr& argument : claim.arguments) {
        const auto* quantified = std::get_if<kernel::Forall>(&instance.node);
        if (quantified == nullptr) {
            report(engine, diagnostics::Category::UnsupportedSemantics, argument.provenance.range.begin,
                   "law '" + obligation.subject +
                       "' is named at more arguments than it "
                       "quantifies over");
            return std::nullopt;
        }

        std::expected<kernel::Term, Failure> term = lowering.lower(argument);
        if (!term) {
            report(engine, diagnostics::Category::UnsupportedSemantics, term.error().location,
                   "proof '" + proof.name + "' claims law '" + obligation.subject +
                       "' at a term the formal core cannot state: " + term.error().reason);
            return std::nullopt;
        }
        instance = kernel::instantiate(*quantified->body, *term);
    }

    for (auto parameter = proof.parameters.rbegin(); parameter != proof.parameters.rend(); ++parameter) {
        const std::optional<kernel::Type> binder = lower_type(parameter->type);
        if (!binder.has_value()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, proof.range.begin,
                   "proof '" + proof.name + "' quantifies over '" + vir::describe(parameter->type) +
                       "', which the formal core does not represent");
            return std::nullopt;
        }
        instance = kernel::Proposition::for_all(*binder, std::move(instance));
    }

    return instance;
}

// The evidence a referenced proof supplies once it is instantiated.
//
// Each argument is one universal elimination. The proof term records the
// proposition it eliminates from so the kernel can check that step itself; the
// proposition tracked alongside is this layer's own account of where the
// elimination has got to, and the kernel derives it again independently.
struct Instantiation {
    kernel::Proposition proposition;
    kernel::ProofTerm term;

    // Whether an instantiation argument mentions a variable. Such evidence
    // means something only underneath the binders it was stated in, so it
    // cannot stand where those binders have not been introduced.
    bool open = false;
};

bool mentions_variable(const kernel::Term& term) {
    if (const auto* call = std::get_if<kernel::Call>(&term.node))
        return std::ranges::any_of(call->arguments, [](const auto& argument) { return mentions_variable(argument); });
    if (const auto* primitive = std::get_if<kernel::Prim>(&term.node))
        return std::ranges::any_of(primitive->arguments,
                                   [](const auto& argument) { return mentions_variable(argument); });
    return std::holds_alternative<kernel::Var>(term.node);
}

// `depth` is how many binders enclose the goal this evidence is being offered
// for. The proof's parameters are the outermost of them, so an argument naming
// one is lowered against that depth rather than against the parameter list.
std::optional<Instantiation> instantiate_evidence(const vir::Proof& proof, const std::string& evidence,
                                                  Instantiation state, const std::vector<vir::Expr>& arguments,
                                                  std::size_t depth, const DefinitionMap& definitions,
                                                  diagnostics::Engine& engine) {
    TermLowering lowering(definitions, depth);

    for (const vir::Expr& argument : arguments) {
        const source::SourceLocation& location = argument.provenance.range.begin;

        const auto* quantified = std::get_if<kernel::Forall>(&state.proposition.node);
        if (quantified == nullptr) {
            report(engine, diagnostics::Category::ProofFailure, location,
                   "proof '" + evidence +
                       "' is instantiated at more arguments than it "
                       "quantifies over",
                   "at this argument it establishes " + kernel::describe(state.proposition) +
                       ", which quantifies over nothing");
            return std::nullopt;
        }

        const std::optional<kernel::Type> type = lower_type(argument.type);
        if (!type.has_value() || !(*type == quantified->binder)) {
            report(engine, diagnostics::Category::ProofFailure, location,
                   "proof '" + evidence + "' quantifies over '" + kernel::describe(quantified->binder) +
                       "' and cannot be instantiated at a term of type '" + vir::describe(argument.type) + "'");
            return std::nullopt;
        }

        std::expected<kernel::Term, Failure> term = lowering.lower(argument);
        if (!term) {
            report(engine, diagnostics::Category::UnsupportedSemantics, term.error().location,
                   "proof '" + proof.name + "' instantiates '" + evidence +
                       "' at a term the formal core cannot state: " + term.error().reason);
            return std::nullopt;
        }

        kernel::Proposition eliminated = kernel::instantiate(*quantified->body, *term);
        state.open = state.open || mentions_variable(*term);
        state.term = kernel::ProofTerm::forall_elimination(std::move(state.proposition), std::move(state.term), *term);
        state.proposition = std::move(eliminated);
    }

    return state;
}

// The statement a goal makes underneath the quantifiers it leads with, and the
// binders passed on the way.
const kernel::Proposition* under_quantifiers(const kernel::Proposition& goal, std::vector<kernel::Type>& binders) {
    const kernel::Proposition* inner = &goal;
    while (const auto* quantified = std::get_if<kernel::Forall>(&inner->node)) {
        binders.push_back(quantified->binder);
        inner = &*quantified->body;
    }
    return inner;
}

kernel::Term abstract_occurrences(const kernel::Term& term, const kernel::Term& target, std::uint32_t depth,
                                  bool& found);

kernel::Proposition abstract_occurrences(const kernel::Proposition& proposition, const kernel::Term& target,
                                         std::uint32_t depth, bool& found) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&proposition.node)) {
        return kernel::Proposition::for_all(
            quantified->binder, abstract_occurrences(*quantified->body, kernel::shift(target, 1), depth + 1, found));
    }
    if (const auto* implication = std::get_if<kernel::Implies>(&proposition.node)) {
        return kernel::Proposition::implication(abstract_occurrences(*implication->premise, target, depth, found),
                                                abstract_occurrences(*implication->conclusion, target, depth, found));
    }
    if (const auto* conjunction = std::get_if<kernel::And>(&proposition.node)) {
        return kernel::Proposition::conjunction(abstract_occurrences(*conjunction->left, target, depth, found),
                                                abstract_occurrences(*conjunction->right, target, depth, found));
    }
    if (const auto* disjunction = std::get_if<kernel::Or>(&proposition.node)) {
        return kernel::Proposition::disjunction(abstract_occurrences(*disjunction->left, target, depth, found),
                                                abstract_occurrences(*disjunction->right, target, depth, found));
    }
    if (std::holds_alternative<kernel::Falsity>(proposition.node)) {
        return proposition;
    }
    const auto& equality = std::get<kernel::Eq>(proposition.node);
    return kernel::Proposition::equality(equality.type, abstract_occurrences(equality.lhs, target, depth, found),
                                         abstract_occurrences(equality.rhs, target, depth, found));
}

kernel::Term abstract_occurrences(const kernel::Term& term, const kernel::Term& target, std::uint32_t depth,
                                  bool& found) {
    if (term == target) {
        found = true;
        return kernel::Term::variable(kernel::VarIndex{depth});
    }
    if (const auto* call = std::get_if<kernel::Call>(&term.node)) {
        std::vector<kernel::Term> arguments;
        arguments.reserve(call->arguments.size());
        for (const kernel::Term& argument : call->arguments) {
            arguments.push_back(abstract_occurrences(argument, target, depth, found));
        }
        return kernel::Term::call(call->callee, std::move(arguments));
    }
    if (const auto* primitive = std::get_if<kernel::Prim>(&term.node)) {
        std::vector<kernel::Term> arguments;
        arguments.reserve(primitive->arguments.size());
        for (const kernel::Term& argument : primitive->arguments) {
            arguments.push_back(abstract_occurrences(argument, target, depth, found));
        }
        return kernel::Term::primitive(primitive->op, primitive->type, std::move(arguments));
    }
    return term;
}

// The context a rewrite transports through: the goal with every occurrence of
// `target` standing for the hole.
//
// Which occurrences a rewrite transforms is a question about what the author
// meant, so it is settled here rather than in the kernel. Every occurrence is
// the rule, and it is the whole rule: nothing is searched for and nothing is
// weighed. The context is then handed to the kernel as part of the proof term,
// and the kernel checks that filling it yields the goal, so a choice made here
// can only fail to prove something - never prove the wrong thing.
std::optional<kernel::Proposition> make_rewrite_context(const kernel::Proposition& goal, const kernel::Term& target) {
    // The context stands underneath one more binder than the goal does - the
    // hole itself - so the goal is restated for that depth before the
    // occurrences are taken out of it.
    bool found = false;
    kernel::Proposition motive = abstract_occurrences(kernel::shift(goal, 1), kernel::shift(target, 1), 0, found);
    if (!found) {
        return std::nullopt;
    }
    return motive;
}

kernel::ProofTerm quantify(const std::vector<kernel::Type>& binders, kernel::ProofTerm term) {
    for (const auto& binder : std::views::reverse(binders)) {
        term = kernel::ProofTerm::forall_introduction(binder, std::move(term));
    }
    return term;
}

// How many premises stand between a statement's evidence and the goal.
//
// Deciding this needs the two propositions and nothing else, so it is settled
// before any statement is consumed to discharge them. `exact` offers the goal
// itself and so discharges nothing: peeling a premise is what `apply` means.
bool convertible_equality(const kernel::Context& context, const kernel::Proposition& available,
                          const kernel::Proposition& goal) {
    const auto* from = std::get_if<kernel::Eq>(&available.node);
    const auto* to = std::get_if<kernel::Eq>(&goal.node);
    if (from == nullptr || to == nullptr || !(from->type == to->type))
        return false;
    const auto left_from = kernel::normalize(context, from->lhs, kernel::CoreLimits{});
    const auto left_to = kernel::normalize(context, to->lhs, kernel::CoreLimits{});
    const auto right_from = kernel::normalize(context, from->rhs, kernel::CoreLimits{});
    const auto right_to = kernel::normalize(context, to->rhs, kernel::CoreLimits{});
    return left_from && left_to && right_from && right_to && *left_from == *left_to && *right_from == *right_to;
}

// Definitional conversion is derived from the existing equality rule. Both
// conversions are explicit reflexivity evidence, checked independently by the
// kernel; normalization in the producer merely chooses when to offer them.
kernel::ProofTerm convert_equality(const kernel::Proposition& available, const kernel::Proposition& goal,
                                   kernel::ProofTerm evidence) {
    const auto* from = std::get_if<kernel::Eq>(&available.node);
    const auto* to = std::get_if<kernel::Eq>(&goal.node);
    if (available == goal || from == nullptr || to == nullptr || !(from->type == to->type))
        return evidence;
    const auto hole = kernel::Term::variable(kernel::VarIndex{0});
    auto right = kernel::ProofTerm::equality_elimination(
        to->type, to->rhs, from->rhs, kernel::Proposition::equality(to->type, kernel::shift(from->lhs, 1), hole),
        kernel::ProofTerm::reflexivity(), std::move(evidence));
    return kernel::ProofTerm::equality_elimination(
        to->type, to->lhs, from->lhs, kernel::Proposition::equality(to->type, hole, kernel::shift(to->rhs, 1)),
        kernel::ProofTerm::reflexivity(), std::move(right));
}

std::optional<std::size_t> premises_before_the_goal(const kernel::Context& context,
                                                    const kernel::Proposition& available,
                                                    const kernel::Proposition& goal, bool exact, std::string& reason) {
    if (exact) {
        return available == goal || convertible_equality(context, available, goal) ? std::optional<std::size_t>{0}
                                                                                   : std::nullopt;
    }

    const kernel::Proposition* current = &available;
    std::size_t discharged = 0;
    while (true) {
        if (conclusion_is_applicable(*current, goal, reason)) {
            return discharged;
        }
        const auto* implication = std::get_if<kernel::Implies>(&current->node);
        if (implication == nullptr) {
            return std::nullopt;
        }
        current = &*implication->conclusion;
        ++discharged;
    }
}

// A proof body under lowering: the evidence already built for the proofs it
// names, the premises standing in scope, and how far through the statement
// sequence the lowering has got.
//
// Statements are read once, in written order. A statement that leaves a goal
// behind is followed by the statements that close it, so the sequence is walked
// exactly as it was written and is never searched.
struct Body {
    const vir::Proof& proof;
    const kernel::Context& context;
    const DefinitionMap& definitions;
    const std::vector<WrittenProof>& built;
    const std::map<std::uint32_t, std::size_t>& built_index;
    std::vector<std::pair<std::uint32_t, kernel::Proposition>> assumptions;
    std::uint32_t assumed = 0;
    std::size_t cursor = 0;

    // How many binders enclose the goal being proved. A goal states a
    // proposition that may quantify over binders of its own, so this is not the
    // proof's parameter count: it is what every term written here is stated
    // underneath, and what keeps a name in a statement denoting the same
    // variable however deeply the goal nests (SPEC.md 8).
    std::size_t depth = 0;
    const std::vector<vir::ProofStep>* steps = &proof.steps;
    source::SourceLocation body_location = proof.range.begin;

    // The types of those binders, outermost first, so `depth` is always their
    // count. A claim made inside the proof but checked on its own, an omitted
    // case (SPEC.md CASE-016), is closed over them.
    std::vector<kernel::Type> binders = {};

    // The omitted cases this body has established, each an obligation of its
    // own. They are submitted with the proof only once the proof is admitted,
    // so a refused proof leaves none behind.
    std::vector<Obligation>* omissions = nullptr;

    // The trusted laws the whole proof rests on, supposed outside everything
    // else in it, the first outermost (SPEC.md TRUSTED-006). They are not
    // standing premises: `assume` cannot name one and `contradiction` does not
    // reason from one unless a statement names it as evidence (TRUSTED-008).
    const std::vector<TrustedPremise>* trusted = nullptr;
};

// Where a trusted premise stands among the hypotheses in scope. The trusted
// premises are introduced before anything the body assumes, so each counts back
// past every standing premise.
std::optional<kernel::HypothesisIndex> trusted_hypothesis(const Body& body, vir::LawId law) {
    if (body.trusted == nullptr)
        return std::nullopt;
    const auto found =
        std::ranges::find_if(*body.trusted, [law](const TrustedPremise& premise) { return premise.law == law; });
    if (found == body.trusted->end())
        return std::nullopt;
    const auto position = static_cast<std::size_t>(std::distance(body.trusted->begin(), found));
    return kernel::HypothesisIndex{
        static_cast<std::uint32_t>(body.assumptions.size() + (body.trusted->size() - 1 - position))};
}

// Introduces binders in front of everything a body stands under, for as long as
// this lives: its depth, its binder types, and every premise already standing,
// which the kernel sees shifted past the new binders (check.cpp). Every premise
// in `Body::assumptions` is therefore stated at `Body::depth`, which is what
// lets a statement use any of them - and what lets `contradiction` state all of
// them as facts - without knowing where each was assumed.
class Underneath {
  public:
    Underneath(Body& body, const std::vector<kernel::Type>& binders)
        : body_(body),
          depth_(body.depth),
          count_(body.binders.size()),
          assumptions_(body.assumptions) {
        const auto added = static_cast<std::uint32_t>(binders.size());
        body.depth += binders.size();
        body.binders.insert(body.binders.end(), binders.begin(), binders.end());
        for (auto& assumption : body.assumptions) {
            assumption.second = kernel::shift(assumption.second, added);
        }
    }
    // Only ever shrinks: erasing the binders it added cannot allocate, where
    // resize() has a growing path that can throw out of a destructor.
    ~Underneath() {
        body_.depth = depth_;
        body_.binders.erase(body_.binders.begin() + static_cast<std::ptrdiff_t>(count_), body_.binders.end());
        body_.assumptions = std::move(assumptions_);
    }
    Underneath(const Underneath&) = delete;
    Underneath& operator=(const Underneath&) = delete;
    Underneath(Underneath&&) = delete;
    Underneath& operator=(Underneath&&) = delete;

  private:
    Body& body_;
    std::size_t depth_;
    std::size_t count_;
    std::vector<std::pair<std::uint32_t, kernel::Proposition>> assumptions_;
};

std::optional<kernel::ProofTerm> prove(Body& body, const kernel::Proposition& goal, diagnostics::Engine& engine);

// `assume h : P;` names the premise the goal supposes. It introduces the
// quantifiers standing in front of that premise, because a premise stated of
// the proof's parameters is only visible underneath them.
//
// Nothing is assumed that the goal did not already suppose: the proposition
// written here is compared with the goal's own premise, and the hypothesis
// exists only because the implication introduction below puts it there.
std::optional<kernel::ProofTerm> suppose(Body& body, const vir::ProofStep& step, const vir::AssumeStep& assumed,
                                         const kernel::Proposition& goal, diagnostics::Engine& engine) {
    const std::uint32_t position = body.assumed++;

    std::vector<kernel::Type> binders;
    const kernel::Proposition* inner = under_quantifiers(goal, binders);
    const std::size_t depth = body.depth + binders.size();

    auto written = lower_proposition(assumed.proposition, body.definitions, depth);
    if (!written) {
        report(engine, diagnostics::Category::UnsupportedSemantics, step.location,
               "the assumed proposition cannot be stated: " + written.error().reason);
        return std::nullopt;
    }
    // Case premises already stand in the context. Naming one does not create
    // another assumption or consume an implication from the enclosing goal.
    constexpr auto anonymous = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t index = body.assumptions.size(); index > 0; --index) {
        auto& premise = body.assumptions[index - 1];
        if (premise.first == anonymous &&
            kernel::shift(premise.second, static_cast<std::uint32_t>(binders.size())) == *written) {
            premise.first = position;
            auto result = prove(body, goal, engine);
            body.assumptions[index - 1].first = anonymous;
            return result;
        }
    }
    const auto* implication = std::get_if<kernel::Implies>(&inner->node);
    if (implication == nullptr) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + assumed.name + "' has no matching premise to stand for",
               "the goal here is " + kernel::describe(*inner));
        return std::nullopt;
    }

    if (!(*written == *implication->premise)) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + assumed.name + "' does not name the premise this goal supposes",
               "it states " + kernel::describe(*written) + ", and the premise is " +
                   kernel::describe(*implication->premise));
        return std::nullopt;
    }

    std::optional<kernel::ProofTerm> rest;
    {
        // The premise is stated underneath the binders just introduced, so it
        // joins the standing premises after they have been shifted past them.
        const Underneath introduced(body, binders);
        body.assumptions.emplace_back(position, *implication->premise);
        rest = prove(body, *implication->conclusion, engine);
    }
    if (!rest.has_value()) {
        return std::nullopt;
    }

    return quantify(binders, kernel::ProofTerm::implication_introduction(*implication->premise, std::move(*rest)));
}

// The evidence a statement names, before it is instantiated: a proof this unit
// has already built, or a premise standing in scope.
std::optional<Instantiation> named_evidence(const Body& body, const vir::ProofStep& step,
                                            const vir::Reference& reference, diagnostics::Engine& engine) {
    if (const auto* assumed = std::get_if<vir::HypothesisRef>(&reference.node)) {
        const auto found = std::ranges::find_if(
            body.assumptions, [&assumed](const auto& entry) { return entry.first == assumed->assumption; });
        if (found == body.assumptions.end()) {
            report(engine, diagnostics::Category::ProofFailure, step.location,
                   "the premise '" + reference.name + "' names is not in scope here",
                   "it was assumed for a goal that has already been closed");
            return std::nullopt;
        }

        // A premise is named from the inside out, so its index counts back from
        // the most recently assumed one.
        const auto position = static_cast<std::uint32_t>(
            body.assumptions.size() - 1 - static_cast<std::size_t>(std::distance(body.assumptions.begin(), found)));
        return Instantiation{found->second, kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{position})};
    }

    // Every trusted law this proof rests on was collected from its statements
    // before it was lowered, so a law missing here is a fault in that
    // collection. It is refused rather than left out of what the kernel checks.
    const auto unaccounted = [&](const std::string& law) {
        report(engine, diagnostics::Category::Internal, step.location,
               "trusted law '" + law + "' is used by proof '" + body.proof.name + "' but is not among its premises");
        return std::nullopt;
    };

    if (const auto* trusted = std::get_if<vir::TrustedLawRef>(&reference.node)) {
        const std::optional<kernel::HypothesisIndex> index = trusted_hypothesis(body, trusted->law);
        if (!index.has_value()) {
            return unaccounted(reference.name);
        }
        const auto& premise = *std::ranges::find_if(
            *body.trusted, [&trusted](const TrustedPremise& candidate) { return candidate.law == trusted->law; });
        return Instantiation{premise.proposition, kernel::ProofTerm::hypothesis(*index)};
    }

    const auto source = body.built_index.find(std::get<vir::ProofRef>(reference.node).proof.value);
    const WrittenProof& used = body.built[source->second];

    // A proof established relative to trusted laws is used relative to the
    // same ones: each premise it supposes is discharged by this body's own
    // supposition of that law, so the dependency carries over and is never
    // dropped on the way (TRUST.md TCB-TRUST-003).
    kernel::ProofTerm term = used.term;
    kernel::Proposition current = relative_to(used.assumptions, used.goal);
    for (const TrustedPremise& premise : used.assumptions) {
        const std::optional<kernel::HypothesisIndex> index = trusted_hypothesis(body, premise.law);
        if (!index.has_value()) {
            return unaccounted(premise.name);
        }
        kernel::Proposition conclusion = *std::get<kernel::Implies>(current.node).conclusion;
        term = kernel::ProofTerm::implication_elimination(std::move(current), std::move(term),
                                                          kernel::ProofTerm::hypothesis(*index));
        current = std::move(conclusion);
    }
    return Instantiation{used.goal, std::move(term)};
}

// `rewrite e;` transforms the goal with an equality and leaves what it
// transformed it into to prove.
//
// The quantifiers the goal leads with are introduced first, because evidence
// stated of the proof's parameters only reaches the goal underneath them. The
// equality is not asserted here: it is evidence like any other, and the kernel
// checks it along with the context this builds.
std::optional<kernel::ProofTerm> transport(Body& body, const vir::ProofStep& step, const vir::RewriteStep& rewritten,
                                           const kernel::Proposition& goal, diagnostics::Engine& engine) {
    std::vector<kernel::Type> binders;
    const kernel::Proposition* inner = under_quantifiers(goal, binders);
    const std::size_t depth = body.depth + binders.size();

    std::optional<Instantiation> evidence = named_evidence(body, step, rewritten.evidence, engine);
    if (!evidence.has_value()) {
        return std::nullopt;
    }

    std::optional<Instantiation> instantiated =
        instantiate_evidence(body.proof, rewritten.evidence.name, std::move(*evidence), rewritten.arguments, depth,
                             body.definitions, engine);
    if (!instantiated.has_value()) {
        return std::nullopt;
    }

    const auto* equality = std::get_if<kernel::Eq>(&instantiated->proposition.node);
    if (equality == nullptr) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + rewritten.evidence.name +
                   "' does not establish an equality, so there is "
                   "nothing for it to rewrite",
               "it establishes " + kernel::describe(instantiated->proposition));
        return std::nullopt;
    }

    std::optional<kernel::Proposition> motive = rewrite_context(*inner, equality->lhs);
    if (!motive.has_value()) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + rewritten.evidence.name + "' rewrites " + kernel::describe(equality->lhs) +
                   ", which does not occur in the goal",
               "the goal here is " + kernel::describe(*inner));
        return std::nullopt;
    }

    const kernel::Proposition remaining = kernel::instantiate(*motive, equality->rhs);
    std::optional<kernel::ProofTerm> rest;
    {
        const Underneath introduced(body, binders);
        rest = prove(body, remaining, engine);
    }
    if (!rest.has_value()) {
        return std::nullopt;
    }

    return quantify(binders, kernel::ProofTerm::equality_elimination(equality->type, equality->lhs, equality->rhs,
                                                                     std::move(*motive), std::move(instantiated->term),
                                                                     std::move(*rest)));
}

// Records an established omission as the obligation CASE-012 and CASE-016
// require, with an origin, provenance and identity of its own and a goal that
// stands apart from the proof it was written in: that the premises standing in
// the omitted case, closed over the binders they stand under, cannot all hold:
// that together they establish `False`. Its evidence is the refutation `absurd`,
// which the kernel checks against that goal on its own.
//
// Every premise in `body` is stated at the body's depth (`Underneath`), so they
// are introduced after all the binders, in the order they were assumed; that
// keeps each hypothesis the refutation names at the same position it had where
// the refutation was built.
void record_omission(const Body& body, const vir::CaseArm& arm, const kernel::ProofTerm& absurd) {
    kernel::Proposition goal = kernel::Proposition::falsity();
    kernel::ProofTerm evidence = absurd;
    for (std::size_t index = body.assumptions.size(); index > 0; --index) {
        const kernel::Proposition& premise = body.assumptions[index - 1].second;
        goal = kernel::Proposition::implication(premise, std::move(goal));
        evidence = kernel::ProofTerm::implication_introduction(premise, std::move(evidence));
    }
    for (const kernel::Type& binder : std::views::reverse(body.binders)) {
        goal = kernel::Proposition::for_all(binder, std::move(goal));
        evidence = kernel::ProofTerm::forall_introduction(binder, std::move(evidence));
    }

    // The refutation may name a trusted law, whose hypothesis stands outside
    // everything closed over above. The claim is therefore made relative to
    // every trusted law of the proof it is written in: more than the refutation
    // may need, never less.
    Obligation obligation;
    if (body.trusted != nullptr) {
        obligation.assumptions = *body.trusted;
        for (TrustedPremise& premise : std::views::reverse(obligation.assumptions)) {
            premise.direct = false;
            evidence = kernel::ProofTerm::implication_introduction(premise.proposition, std::move(evidence));
        }
    }
    obligation.origin = Origin::OmittedCase;
    obligation.subject = "case '" + arm.label + "' of proof '" + body.proof.name + "'";
    obligation.goal = std::move(goal);
    obligation.evidence = std::move(evidence);
    obligation.range = source::SourceRange{arm.location, {}};

    obligation.id = identify_impossibility(Origin::OmittedCase, body.context, obligation.subject, obligation.goal,
                                           body.omissions->size());
    body.omissions->push_back(std::move(obligation));
}

// `contradiction e;` closes the goal from evidence that the context cannot
// occur (GRAMMAR.md 5.6, SPEC.md CASE-011), and `omit label by contradiction e;`
// accounts for an omitted case the same way (GRAMMAR.md 5.7, CASE-004).
//
// The contradiction is between the named evidence and every premise standing
// here, which for an omitted case includes that case's own discriminator
// (CASE-013). It is established on its own, before the goal is looked at: the
// premises are refuted into `False`, which no goal takes part in, and only then
// is the goal closed from that by falsity elimination, whatever its shape. A
// goal that merely follows from the premises therefore establishes nothing
// here, so neither form can close a case whose goal happened to be provable
// while claiming the case cannot occur.
//
// The refutation is linear arithmetic whose certificate the kernel checks
// against constraints it states itself (CASE-014), and a context not shown
// contradictory is an ordinary unproven claim (CASE-005, CASE-015), never an
// impossibility.
std::optional<kernel::ProofTerm> prove_contradiction(Body& body, const vir::ProofStep& step,
                                                     const vir::ContradictionStep& contradiction,
                                                     const kernel::Proposition& goal, diagnostics::Engine& engine,
                                                     const vir::CaseArm* omitted = nullptr) {
    // The goal's own quantifiers come first, as for every statement that names
    // evidence, because an argument may mention them. What stands beneath them
    // is closed whatever it is, so only the binders are kept.
    std::vector<kernel::Type> binders;
    under_quantifiers(goal, binders);
    const Underneath introduced(body, binders);

    const std::string& name = contradiction.evidence.name;
    std::optional<Instantiation> evidence = named_evidence(body, step, contradiction.evidence, engine);
    if (!evidence.has_value()) {
        return std::nullopt;
    }

    std::optional<Instantiation> instantiated = instantiate_evidence(
        body.proof, name, std::move(*evidence), contradiction.arguments, body.depth, body.definitions, engine);
    if (!instantiated.has_value()) {
        return std::nullopt;
    }

    if (!std::holds_alternative<kernel::Eq>(instantiated->proposition.node)) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + name + "' does not establish an equality, so it cannot state a contradiction",
               "it establishes " + kernel::describe(instantiated->proposition));
        return std::nullopt;
    }
    if (!arithmetic_fact(body.context, instantiated->proposition)) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "'" + name +
                   "' establishes an equality linear arithmetic cannot state, so it cannot state a "
                   "contradiction",
               "it establishes " + kernel::describe(instantiated->proposition));
        return std::nullopt;
    }

    // Every standing premise, from the innermost out, so an omitted case's
    // discriminator is never the one left out.
    const kernel::Proposition established = instantiated->proposition;
    std::vector<Standing> standing;
    standing.reserve(body.assumptions.size());
    for (std::size_t index = body.assumptions.size(); index > 0; --index) {
        standing.push_back(Standing{body.assumptions[index - 1].second,
                                    kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{
                                        static_cast<std::uint32_t>(body.assumptions.size() - index)})});
    }

    std::expected<kernel::ProofTerm, Unestablished> absurd = detail::refute(
        body.context,
        kernel::ArithmeticFact{established, kernel::Box<kernel::ProofTerm>{std::move(instantiated->term)}}, standing);
    if (!absurd.has_value()) {
        if (absurd.error().kind == Unestablished::Kind::Unreadable) {
            report(engine, diagnostics::Category::ProofFailure, step.location,
                   "the premises standing here cannot be stated as linear arithmetic, so no contradiction can be "
                   "read from them",
                   absurd.error().detail);
        } else if (omitted != nullptr) {
            report(engine, diagnostics::Category::ProofFailure, omitted->location,
                   "omitted case '" + omitted->label + "' is not shown to be impossible",
                   "'" + name + "' establishes " + kernel::describe(established) +
                       ", and no contradiction with the premises standing in that case, including its own "
                       "discriminator, was found");
        } else {
            report(engine, diagnostics::Category::ProofFailure, step.location,
                   "'" + name + "' does not state a contradiction",
                   "it establishes " + kernel::describe(established) +
                       ", and no contradiction with the premises standing here was found");
        }
        return std::nullopt;
    }

    if (omitted != nullptr && body.omissions != nullptr) {
        record_omission(body, *omitted, *absurd);
    }

    return quantify(binders, kernel::ProofTerm::falsity_elimination(std::move(*absurd)));
}

// At most this many arms in one statement, so malformed VIR cannot make
// evidence construction grow without bound. Proof-resource limits apply too.
constexpr std::size_t kMaxCaseArms = 64;

// Case splitting is derived evidence, not a new rule.
//
// A provider describes a partition as a list of discriminator conditions. Each
// is an ordinary modeled comparison, and machine comparison is total, so
// conditional elimination on one splits any goal into two branches the kernel
// checks independently. Splitting on the discriminators in turn leaves one
// remaining branch, in which every discriminator is known false; that is the
// tail case, and it receives their conjunction. No completeness claim about the
// representation reaches the kernel: exhaustiveness is a property of evidence
// the kernel rechecks, and a provider that described the wrong partition can
// only fail to produce a proof, never forge one.
std::optional<kernel::ProofTerm> prove_cases(Body& body, const vir::ProofStep& step, const vir::CasesStep& cases,
                                             const kernel::Proposition& goal, diagnostics::Engine& engine) {
    std::vector<kernel::Type> binders;
    const auto* inner = under_quantifiers(goal, binders);
    Body scoped = body;
    scoped.depth += binders.size();
    scoped.binders.insert(scoped.binders.end(), binders.begin(), binders.end());
    for (auto& assumption : scoped.assumptions)
        assumption.second = kernel::shift(assumption.second, static_cast<std::uint32_t>(binders.size()));

    // The partition is asked of the provider here, not taken from the arms.
    // What the frontend recorded is only which case each arm claims; whether
    // those claims cover the representation is decided against the provider's
    // own description.
    const decomposition::Decomposition decomposed = decomposition::decompose({cases.subject, step.location});
    const auto* sum = std::get_if<decomposition::SumDecomposition>(&decomposed);
    if (sum == nullptr) {
        report(engine, diagnostics::Category::ProofFailure, step.location,
               "the subject of this case split has no sum decomposition");
        return std::nullopt;
    }

    TermLowering lowering(body.definitions, scoped.depth);
    std::vector<kernel::Term> discriminators;
    for (const decomposition::CaseDescriptor& descriptor : sum->cases) {
        auto condition = lowering.lower(descriptor.discriminator);
        if (!condition) {
            report(engine, diagnostics::Category::ProofFailure, step.location,
                   "case '" + descriptor.label.text + "' cannot be stated: " + condition.error().reason);
            return std::nullopt;
        }
        discriminators.push_back(std::move(*condition));
    }

    // Which arm proves which case, checked against the provider's partition.
    const bool residual_required = sum->exhaustiveness == decomposition::ExhaustivenessModel::ResidualRequired;
    std::vector<const vir::CaseArm*> claimed(sum->cases.size(), nullptr);
    const vir::CaseArm* residual = nullptr;
    for (const vir::CaseArm& arm : cases.arms) {
        if (arm.descriptor.has_value()) {
            if (*arm.descriptor >= claimed.size() || claimed[*arm.descriptor] != nullptr) {
                report(engine, diagnostics::Category::ProofFailure, arm.location, "malformed case evidence");
                return std::nullopt;
            }
            claimed[*arm.descriptor] = &arm;
            continue;
        }
        if (residual != nullptr || !residual_required) {
            report(engine, diagnostics::Category::ProofFailure, arm.location, "malformed case evidence");
            return std::nullopt;
        }
        residual = &arm;
    }
    // A partition with no residual case and no cases at all would claim the
    // subject has no states, which is not something evidence can establish.
    if (std::ranges::find(claimed, nullptr) != claimed.end() || (residual_required && residual == nullptr) ||
        (!residual_required && sum->cases.empty()) || cases.arms.size() > kMaxCaseArms) {
        report(engine, diagnostics::Category::ProofFailure, step.location, "incomplete case evidence");
        return std::nullopt;
    }

    // The branch in which every discriminator is false. When the partition
    // needs a residual case that is its arm; otherwise the last named case is
    // exactly the negation of the others, and splitting stops one short of it.
    const std::size_t splits = residual_required ? sum->cases.size() : sum->cases.size() - 1;
    const vir::CaseArm& tail = residual_required ? *residual : *claimed.back();

    constexpr auto anonymous = std::numeric_limits<std::uint32_t>::max();
    const auto arm_proof = [&](Body context, const vir::CaseArm& arm) -> std::optional<kernel::ProofTerm> {
        context.steps = &arm.steps;
        context.cursor = 0;
        context.body_location = arm.location;
        // An omitted case is discharged by one contradiction and nothing else
        // (CASE-004 clause 2). The case's discriminator already stands in this
        // context, so the evidence is checked under it exactly as an arm's body
        // would be (CASE-013), and the omission is recorded as an obligation of
        // its own (CASE-016).
        if (arm.omitted) {
            const auto* discharge =
                arm.steps.size() == 1 ? std::get_if<vir::ContradictionStep>(&arm.steps[0].node) : nullptr;
            if (discharge == nullptr) {
                report(engine, diagnostics::Category::ProofFailure, arm.location, "malformed omitted case");
                return std::nullopt;
            }
            return prove_contradiction(context, arm.steps[0], *discharge, *inner, engine, &arm);
        }
        auto evidence = prove(context, *inner, engine);
        if (evidence && context.cursor != arm.steps.size()) {
            report(engine, diagnostics::Category::ProofFailure, arm.steps[context.cursor].location,
                   "case arm has already closed its goal");
            return std::nullopt;
        }
        return evidence;
    };

    const auto split = [&](auto&& self, Body context, std::size_t position) -> std::optional<kernel::ProofTerm> {
        if (position == splits) {
            if (position == 0) {
                return arm_proof(context, tail);
            }
            // Supply the tail case's fact: the left-associated conjunction of
            // every exclusion, in the partition's own order, built from the
            // path facts this branch already carries.
            auto fact = context.assumptions[context.assumptions.size() - position].second;
            auto evidence = kernel::ProofTerm::hypothesis({static_cast<std::uint32_t>(position - 1)});
            for (std::size_t index = 1; index < position; ++index) {
                fact = kernel::Proposition::conjunction(
                    std::move(fact), context.assumptions[context.assumptions.size() - position + index].second);
                evidence = kernel::ProofTerm::conjunction_introduction(
                    std::move(evidence),
                    kernel::ProofTerm::hypothesis({static_cast<std::uint32_t>(position - index - 1)}));
            }
            context.assumptions.emplace_back(anonymous, fact);
            auto arm = arm_proof(context, tail);
            if (!arm)
                return std::nullopt;
            return kernel::ProofTerm::implication_elimination(
                kernel::Proposition::implication(fact, *inner),
                kernel::ProofTerm::implication_introduction(fact, std::move(*arm)), std::move(evidence));
        }

        const kernel::Term& condition = discriminators[position];
        const auto positive = kernel::predicate(condition, true);
        const auto negative = kernel::predicate(condition, false);
        Body holds = context;
        holds.assumptions.emplace_back(anonymous, positive);
        auto at_case = arm_proof(holds, *claimed[position]);
        if (!at_case)
            return std::nullopt;
        context.assumptions.emplace_back(anonymous, negative);
        auto otherwise = self(self, context, position + 1);
        if (!otherwise)
            return std::nullopt;
        return kernel::ProofTerm::conditional_elimination(
            kernel::Type{kernel::kBoolean}, condition, kernel::Term::literal(kernel::kBoolean, 1),
            kernel::Term::literal(kernel::kBoolean, 1), kernel::shift(*inner, 1),
            kernel::ProofTerm::implication_introduction(positive, std::move(*at_case)),
            kernel::ProofTerm::implication_introduction(negative, std::move(*otherwise)));
    };

    auto evidence = split(split, scoped, 0);
    if (!evidence)
        return std::nullopt;
    return quantify(binders, std::move(*evidence));
}

std::optional<kernel::ProofTerm> prove(Body& body, const kernel::Proposition& goal, diagnostics::Engine& engine) {
    const vir::Proof& proof = body.proof;

    if (body.cursor >= body.steps->size()) {
        report(engine, diagnostics::Category::ProofFailure, body.body_location,
               "proof '" + proof.name + "' leaves a goal open",
               "nothing in its body establishes " + kernel::describe(goal));
        return std::nullopt;
    }

    const vir::ProofStep& step = (*body.steps)[body.cursor++];

    if (const auto* product = std::get_if<vir::ProductStep>(&step.node)) {
        const auto descriptor = decomposition::decompose({product->subject, step.location});
        if (!std::holds_alternative<decomposition::ProductDecomposition>(descriptor)) {
            report(engine, diagnostics::Category::ProofFailure, step.location, "malformed product decomposition");
            return std::nullopt;
        }
        Body nested = body;
        nested.steps = &product->steps;
        nested.cursor = 0;
        nested.body_location = step.location;
        auto result = prove(nested, goal, engine);
        if (result && nested.cursor != product->steps.size()) {
            report(engine, diagnostics::Category::ProofFailure, step.location, "unused product proof statements");
            return std::nullopt;
        }
        return result;
    }

    if (const auto* cases = std::get_if<vir::CasesStep>(&step.node))
        return prove_cases(body, step, *cases, goal, engine);

    if (std::holds_alternative<vir::ReflexivityStep>(step.node)) {
        return definitional_evidence(goal);
    }

    if (const auto* assumed = std::get_if<vir::AssumeStep>(&step.node)) {
        return suppose(body, step, *assumed, goal, engine);
    }

    if (const auto* rewritten = std::get_if<vir::RewriteStep>(&step.node)) {
        return transport(body, step, *rewritten, goal, engine);
    }

    if (const auto* contradiction = std::get_if<vir::ContradictionStep>(&step.node)) {
        return prove_contradiction(body, step, *contradiction, goal, engine);
    }

    const auto* exact = std::get_if<vir::ExactStep>(&step.node);
    const vir::Reference& reference = exact != nullptr ? exact->evidence : std::get<vir::ApplyStep>(step.node).evidence;
    const std::vector<vir::Expr>& arguments =
        exact != nullptr ? exact->arguments : std::get<vir::ApplyStep>(step.node).arguments;

    std::vector<kernel::Type> binders;
    const kernel::Proposition* inner = under_quantifiers(goal, binders);
    const std::size_t depth = body.depth + binders.size();

    std::optional<Instantiation> evidence = named_evidence(body, step, reference, engine);
    if (!evidence.has_value()) {
        return std::nullopt;
    }

    std::optional<Instantiation> instantiated =
        instantiate_evidence(proof, reference.name, std::move(*evidence), arguments, depth, body.definitions, engine);
    if (!instantiated.has_value()) {
        return std::nullopt;
    }

    // Two readings of the statement, in a fixed order: the goal as it stands,
    // then the goal with its own quantifiers introduced. An argument may be a
    // closed term, or it may mention a variable, in which case the statement it
    // leaves means something only underneath the binders it was stated in and
    // the second reading is the only one available. Which reading the goal asks
    // for is settled by comparing propositions, never by searching.
    std::string reason;
    std::optional<std::size_t> discharge;
    if (!instantiated->open || binders.empty()) {
        discharge = premises_before_the_goal(body.context, instantiated->proposition, goal, exact != nullptr, reason);
    }
    if (discharge.has_value()) {
        binders.clear();
    } else if (!binders.empty()) {
        std::string deeper;
        discharge = premises_before_the_goal(body.context, instantiated->proposition, *inner, exact != nullptr, deeper);
    }

    if (!discharge.has_value()) {
        std::string note = "it establishes " + kernel::describe(instantiated->proposition);
        // A binder a proposition writes for itself has no name a statement can
        // use, so evidence left quantified over one cannot be instantiated here.
        if (std::holds_alternative<kernel::Forall>(instantiated->proposition.node) &&
            !std::holds_alternative<kernel::Forall>(inner->node)) {
            note += ", which stays quantified over a variable no statement here can name";
        }
        note += ", and the goal is " + kernel::describe(goal);
        report(engine, diagnostics::Category::ProofFailure, step.location,
               exact != nullptr ? "'" + reference.name + "' does not prove what proof '" + proof.name + "' claims"
                                : "the conclusion of '" + reference.name + "' cannot be applied to what proof '" +
                                      proof.name + "' claims: " + reason,
               std::move(note));
        return std::nullopt;
    }

    kernel::ProofTerm term = std::move(instantiated->term);
    kernel::Proposition current = std::move(instantiated->proposition);
    {
        // A premise left by the reading that introduced the goal's quantifiers is
        // stated underneath them.
        const Underneath introduced(body, binders);
        for (std::size_t remaining = *discharge; remaining > 0; --remaining) {
            const auto& implication = std::get<kernel::Implies>(current.node);
            kernel::Proposition premise = *implication.premise;
            kernel::Proposition conclusion = *implication.conclusion;

            // The premise this application leaves is a goal like any other, and
            // the statements that follow are what close it.
            std::optional<kernel::ProofTerm> discharged = prove(body, premise, engine);
            if (!discharged.has_value()) {
                return std::nullopt;
            }
            term =
                kernel::ProofTerm::implication_elimination(std::move(current), std::move(term), std::move(*discharged));
            current = std::move(conclusion);
        }
    }

    const auto& target = binders.empty() ? goal : *inner;
    if (convertible_equality(body.context, current, target)) {
        term = convert_equality(current, target, std::move(term));
    }
    return quantify(binders, std::move(term));
}

// Lowers each written proof into a kernel proof term.
//
// A step is lowered only once the proof it names has been lowered, so the
// dependency graph is traversed in order and anything left over is circular.
// No step is admitted on the strength of what it is called: `exact` must offer
// the claimed proposition itself, and `apply` must offer a conclusion that
// proposition can accept. Both then go to the kernel like any other evidence.
void lower_proofs(const vir::Module& module, const elaboration::Result& elaborated, const DefinitionMap& definitions,
                  Program& program, diagnostics::Engine& engine) {
    program.refused_proofs = elaborated.laws_with_refused_proofs;

    std::map<std::uint32_t, const Obligation*> goals;
    std::map<std::uint32_t, const Obligation*> direct_goals;
    for (const Obligation& obligation : program.obligations) {
        if (obligation.law.has_value()) {
            goals.emplace(obligation.law->value, &obligation);
        }
        if (obligation.proof)
            direct_goals.emplace(obligation.proof->value, &obligation);
    }

    std::set<std::uint32_t> declared;
    std::vector<const vir::Proof*> pending;
    for (const vir::Proof& proof : module.proofs) {
        declared.insert(proof.id.value);
        if (proof.law ? goals.contains(proof.law->value) : direct_goals.contains(proof.id.value)) {
            pending.push_back(&proof);
        }
    }

    std::map<std::uint32_t, std::size_t> lowered; // proof id -> index in program.proofs
    std::set<std::uint32_t> refused;
    // The omitted cases admitted proofs established. They join the program's
    // obligations only after the loop, which holds pointers into that list.
    std::vector<Obligation> established_omissions;

    bool progress = true;
    while (progress) {
        progress = false;
        for (auto candidate = pending.begin(); candidate != pending.end();) {
            const vir::Proof& proof = **candidate;
            const Obligation& obligation = proof.law ? *goals.at(proof.law->value) : *direct_goals.at(proof.id.value);

            const auto refuse = [&] {
                refused.insert(proof.id.value);
                if (proof.law)
                    program.refused_proofs.push_back(*proof.law);
                candidate = pending.erase(candidate);
                progress = true;
            };

            // Evidence is built in dependency order: a statement's proof must
            // already have a term before this body can be lowered at all, which
            // is what keeps circular evidence from ever producing one.
            bool admitted = true;
            bool ready = true;
            // The trusted laws this proof rests on: those it names, and those
            // every proof it uses rests on. Keyed by law, so they are supposed
            // in declaration order whatever order they are met in.
            std::map<vir::LawId, TrustedPremise> premises;
            const auto rests_on = [&premises](const TrustedPremise& premise, bool direct) {
                auto [entry, added] = premises.emplace(premise.law, premise);
                entry->second.direct = added ? direct : entry->second.direct || direct;
            };
            std::vector<const vir::ProofStep*> dependencies;
            const auto collect = [&](auto&& self, const std::vector<vir::ProofStep>& steps) -> void {
                for (const auto& step : steps) {
                    dependencies.push_back(&step);
                    if (const auto* product = std::get_if<vir::ProductStep>(&step.node))
                        self(self, product->steps);
                    if (const auto* cases = std::get_if<vir::CasesStep>(&step.node))
                        for (const auto& arm : cases->arms)
                            self(self, arm.steps);
                }
            };
            collect(collect, proof.steps);
            for (const vir::ProofStep* dependency : dependencies) {
                const vir::ProofStep& step = *dependency;
                const vir::Reference* reference = nullptr;
                if (const auto* used = std::get_if<vir::ExactStep>(&step.node)) {
                    reference = &used->evidence;
                } else if (const auto* applied = std::get_if<vir::ApplyStep>(&step.node)) {
                    reference = &applied->evidence;
                } else if (const auto* rewritten = std::get_if<vir::RewriteStep>(&step.node)) {
                    reference = &rewritten->evidence;
                } else if (const auto* refuted = std::get_if<vir::ContradictionStep>(&step.node)) {
                    reference = &refuted->evidence;
                }
                if (reference == nullptr) {
                    continue;
                }
                if (const auto* trusted = std::get_if<vir::TrustedLawRef>(&reference->node)) {
                    // Only a law that is itself an explicit assumption may be
                    // one, and it is supposed as exactly the proposition it
                    // states (TRUST.md TCB-TRUST-009). Anything else named here
                    // would be reported as trust it is not, so it is refused.
                    const auto law = goals.find(trusted->law.value);
                    if (law == goals.end() || !law->second->trusted) {
                        report(engine, diagnostics::Category::ProofFailure, step.location,
                               "trusted law '" + reference->name + "' has no stated proposition, so proof '" +
                                   proof.name + "' has no evidence",
                               "the reason the law was not stated is reported above");
                        admitted = false;
                        break;
                    }
                    rests_on(TrustedPremise{trusted->law, law->second->subject, law->second->id,
                                            law->second->range.begin, law->second->goal},
                             true);
                    continue;
                }
                const auto* named = std::get_if<vir::ProofRef>(&reference->node);
                if (named == nullptr) {
                    continue; // a premise, which needs nothing built
                }
                if (!declared.contains(named->proof.value) || refused.contains(named->proof.value)) {
                    report(engine, diagnostics::Category::ProofFailure, step.location,
                           "proof '" + reference->name + "' was not admitted, so proof '" + proof.name +
                               "' has no evidence",
                           "the reason it was not admitted is reported above");
                    admitted = false;
                    break;
                }
                if (!lowered.contains(named->proof.value)) {
                    ready = false;
                    break;
                }
                for (const TrustedPremise& premise : program.proofs[lowered.at(named->proof.value)].assumptions) {
                    rests_on(premise, false);
                }
            }

            if (!admitted) {
                refuse();
                continue;
            }
            if (!ready) {
                ++candidate; // its evidence is not built yet
                continue;
            }

            const std::optional<kernel::Proposition> claimed =
                claimed_proposition(proof, obligation, definitions, engine);
            if (!claimed.has_value()) {
                refuse();
                continue;
            }

            std::vector<TrustedPremise> assumptions;
            assumptions.reserve(premises.size());
            for (auto& [law, premise] : premises) {
                assumptions.push_back(std::move(premise));
            }

            std::vector<Obligation> omissions;
            Body body{proof, program.context, definitions, program.proofs, lowered, {}, 0, 0};
            body.omissions = &omissions;
            body.trusted = &assumptions;
            std::optional<kernel::ProofTerm> term = prove(body, *claimed, engine);
            // The proof is closed over the trusted laws it rests on, so what the
            // kernel checks is its claim relative to exactly those.
            if (term.has_value()) {
                for (const TrustedPremise& premise : std::views::reverse(assumptions)) {
                    term = kernel::ProofTerm::implication_introduction(premise.proposition, std::move(*term));
                }
            }

            if (term.has_value() && body.cursor < proof.steps.size()) {
                report(engine, diagnostics::Category::ProofFailure, proof.steps[body.cursor].location,
                       "proof '" + proof.name + "' has already closed every goal it states",
                       "the statements before this one leave nothing to prove");
                term.reset();
            }
            if (!term.has_value()) {
                refuse();
                continue;
            }

            WrittenProof written;
            written.id = proof.id;
            written.name = proof.name;
            written.law = proof.law;
            written.goal = *claimed;
            written.closes_law = proof.law.has_value() && *claimed == obligation.goal;
            written.assumptions = std::move(assumptions);
            written.term = std::move(*term);
            written.range = proof.range;

            // A proof that discharges its law is submitted as that law's
            // evidence and is checked there. A proof of one instance has no
            // obligation of its own, so it is checked here: an author's claim
            // is never left standing without the kernel having seen it.
            if (!written.closes_law) {
                const kernel::CheckResult checked =
                    kernel::check(program.context, relative_to(written.assumptions, written.goal), written.term,
                                  kernel::CoreLimits{});
                if (!checked.has_value()) {
                    diagnostics::Diagnostic diagnostic;
                    diagnostic.severity = diagnostics::Severity::Error;
                    diagnostic.category = diagnostics::Category::KernelRejection;
                    diagnostic.message = "proof '" + proof.name + "' does not establish what it claims";
                    diagnostic.location = proof.range.begin;
                    diagnostic.notes.push_back(
                        diagnostics::Note{"claim: " + kernel::describe(written.goal), proof.range.begin});
                    diagnostic.notes.push_back(diagnostics::Note{
                        "the kernel did not accept the evidence: " + checked.error().detail, proof.range.begin});
                    engine.report(std::move(diagnostic));
                    refuse();
                    continue;
                }
            }

            lowered.emplace(proof.id.value, program.proofs.size());
            program.proofs.push_back(std::move(written));
            std::ranges::move(omissions, std::back_inserter(established_omissions));

            candidate = pending.erase(candidate);
            progress = true;
        }
    }

    for (const vir::Proof* proof : pending) {
        report(engine, diagnostics::Category::ProofFailure, proof->steps.front().location,
               "proof '" + proof->name + "' depends on itself through the proofs it uses",
               "this formal core has no induction rule, so written proofs must be acyclic");
        if (proof->law)
            program.refused_proofs.push_back(*proof->law);
    }

    std::map<std::uint32_t, std::string> closed_by;
    for (const WrittenProof& written : program.proofs) {
        if (!written.closes_law || !written.law.has_value()) {
            continue;
        }
        const auto [owner, first] = closed_by.emplace(written.law->value, written.name);
        if (!first) {
            report(engine, diagnostics::Category::CpplSyntax, written.range.begin,
                   "law '" + goals.at(written.law->value)->subject + "' already has a proof",
                   "'" + owner->second +
                       "' establishes it; a law is discharged by exactly one "
                       "written proof, though others may prove instances of it");
        }
    }

    // Writing a proof for a law is taking responsibility for it. If none of the
    // proofs that name a law establishes the law itself, the law stays open:
    // the compiler does not quietly close with its own strategy a goal the
    // author has already said how to establish.
    std::set<std::uint32_t> reported;
    for (const vir::Proof& proof : module.proofs) {
        if (!proof.law || !goals.contains(proof.law->value) || closed_by.contains(proof.law->value)) {
            continue;
        }
        if (program.proof_refused(*proof.law) || !reported.insert(proof.law->value).second) {
            continue;
        }
        report(engine, diagnostics::Category::ProofFailure, proof.range.begin,
               "law '" + goals.at(proof.law->value)->subject +
                   "' is named by a written proof, but nothing establishes the law itself",
               "a proof of one instance does not discharge the law; write a proof that claims "
               "it at its own parameters");
        if (proof.law)
            program.refused_proofs.push_back(*proof.law);
    }

    // Last, because `goals` and `direct_goals` point into this list.
    std::ranges::move(established_omissions, std::back_inserter(program.obligations));
}

// Builds the evidence each claim that a runtime path cannot occur names
// (SPEC.md VERIFIED-023, CASE-013).
//
// A claim's goal is its path's fresh values and supposed facts closed over
// `False`. Its evidence introduces them in turn and, beneath all of them,
// refutes the named proof's conclusion together with every fact into `False`:
// the mechanism an omitted case and a `contradiction` statement use, applied to
// the facts of a path rather than to the premises of a proof. A claim that
// cannot be given evidence is reported once, here, and stands unproven; nothing
// looks for other evidence for it (CASE-005, CASE-015).
void discharge_path_claims(Program& program, diagnostics::Engine& engine) {
    for (PathClaim& claim : program.path_claims) {
        Obligation& obligation = program.obligations[claim.obligation];
        const auto refuse = [&](std::string message, std::string note) {
            obligation.refusal = message;
            report(engine, diagnostics::Category::ProofFailure, claim.location, std::move(message), std::move(note));
        };

        // A name no proof declares was reported where it was resolved.
        if (!claim.proof.has_value()) {
            obligation.refusal = "'" + claim.evidence + "' names no proof declaration";
            continue;
        }
        // An omission in a split's arm uses this mechanism for a claim of its
        // own, and is refused in its own words (SPEC.md CASE-012, CASE-016).
        const bool omission = obligation.origin == Origin::OmittedCase;
        const auto proof = std::ranges::find(program.proofs, *claim.proof, &WrittenProof::id);
        if (proof == program.proofs.end()) {
            refuse("proof '" + claim.evidence + "' was not admitted, so the claim that " +
                       (omission ? obligation.subject : "runtime path '" + obligation.subject + "'") +
                       " cannot occur has no evidence",
                   "the reason it was not admitted is reported above");
            continue;
        }

        // A proof established relative to trusted laws is used relative to the
        // same ones (SPEC.md TRUSTED-006). They are supposed outside everything
        // the claim's goal introduces, so each stands past every fact of the
        // path, and each premise of the proof is discharged by its supposition.
        std::uint32_t facts = 0;
        for (const kernel::Proposition* walk = &obligation.goal;;) {
            if (const auto* quantified = std::get_if<kernel::Forall>(&walk->node)) {
                walk = &*quantified->body;
            } else if (const auto* implication = std::get_if<kernel::Implies>(&walk->node)) {
                ++facts;
                walk = &*implication->conclusion;
            } else {
                break;
            }
        }
        const std::vector<TrustedPremise>& trusted = proof->assumptions;
        kernel::ProofTerm term = proof->term;
        kernel::Proposition named = relative_to(trusted, proof->goal);
        for (std::size_t position = 0; position < trusted.size(); ++position) {
            kernel::Proposition conclusion = *std::get<kernel::Implies>(named.node).conclusion;
            term = kernel::ProofTerm::implication_elimination(
                std::move(named), std::move(term),
                kernel::ProofTerm::hypothesis(
                    kernel::HypothesisIndex{facts + static_cast<std::uint32_t>(trusted.size() - 1 - position)}));
            named = std::move(conclusion);
        }
        bool instantiated = true;
        for (std::size_t index = 0; index < claim.arguments.size() && instantiated; ++index) {
            const auto* quantified = std::get_if<kernel::Forall>(&named.node);
            if (quantified == nullptr) {
                refuse("proof '" + claim.evidence + "' is instantiated at more arguments than it quantifies over",
                       "at this argument it establishes " + kernel::describe(named) +
                           ", which quantifies over nothing");
                instantiated = false;
            } else if (!(quantified->binder == claim.argument_types[index])) {
                refuse("proof '" + claim.evidence + "' quantifies over '" + kernel::describe(quantified->binder) +
                           "' and cannot be instantiated at a term of type '" +
                           kernel::describe(claim.argument_types[index]) + "'",
                       "argument " + std::to_string(index + 1) + " of '" + claim.evidence + "'");
                instantiated = false;
            } else {
                kernel::Proposition eliminated = kernel::instantiate(*quantified->body, claim.arguments[index]);
                term = kernel::ProofTerm::forall_elimination(std::move(named), std::move(term), claim.arguments[index]);
                named = std::move(eliminated);
            }
        }
        if (!instantiated) {
            continue;
        }
        if (!std::holds_alternative<kernel::Eq>(named.node)) {
            refuse("'" + claim.evidence + "' does not establish an equality, so it cannot state a contradiction",
                   "it establishes " + kernel::describe(named));
            continue;
        }
        if (!arithmetic_fact(program.context, named)) {
            refuse("'" + claim.evidence +
                       "' establishes an equality linear arithmetic cannot state, so it cannot state a contradiction",
                   "it establishes " + kernel::describe(named));
            continue;
        }

        // The goal's introductions, outermost first, and the premises they put
        // in scope, each with the number of binders standing where it is.
        struct Introduction {
            const kernel::Proposition* premise = nullptr; // null for a binder
            const kernel::Type* binder = nullptr;
            std::size_t binders = 0;
        };
        std::vector<Introduction> introductions;
        std::size_t binders = 0;
        const kernel::Proposition* current = &obligation.goal;
        while (true) {
            if (const auto* quantified = std::get_if<kernel::Forall>(&current->node)) {
                introductions.push_back(Introduction{nullptr, &quantified->binder, binders++});
                current = &*quantified->body;
            } else if (const auto* implication = std::get_if<kernel::Implies>(&current->node)) {
                introductions.push_back(Introduction{&*implication->premise, nullptr, binders});
                current = &*implication->conclusion;
            } else {
                break;
            }
        }

        // Every fact of the path, innermost first, restated beneath all the
        // binders, where the refutation stands.
        std::vector<Standing> standing;
        std::uint32_t hypothesis = 0;
        for (const Introduction& introduced : std::views::reverse(introductions)) {
            if (introduced.premise == nullptr) {
                continue;
            }
            standing.push_back(
                Standing{kernel::shift(*introduced.premise, static_cast<std::uint32_t>(binders - introduced.binders)),
                         kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{hypothesis++})});
        }

        std::expected<kernel::ProofTerm, Unestablished> absurd = detail::refute(
            program.context, kernel::ArithmeticFact{named, kernel::Box<kernel::ProofTerm>{std::move(term)}}, standing);
        if (!absurd.has_value()) {
            refuse(omission ? "omitted " + obligation.subject + " is not shown to be impossible"
                            : "runtime path '" + obligation.subject + "' is not shown to be unreachable",
                   absurd.error().kind == Unestablished::Kind::Unreadable
                       ? "the facts established on that path cannot be stated as linear arithmetic: " +
                             absurd.error().detail
                       : "'" + claim.evidence + "' establishes " + kernel::describe(named) +
                             (omission ? ", and no contradiction with the facts established on that path, including "
                                         "the case's own discriminator, was found"
                                       : ", and no contradiction with the facts established on that path was found"));
            continue;
        }

        kernel::ProofTerm evidence = std::move(*absurd);
        for (const Introduction& introduced : std::views::reverse(introductions)) {
            evidence = introduced.premise != nullptr
                           ? kernel::ProofTerm::implication_introduction(*introduced.premise, std::move(evidence))
                           : kernel::ProofTerm::forall_introduction(*introduced.binder, std::move(evidence));
        }
        obligation.assumptions = trusted;
        for (TrustedPremise& premise : std::views::reverse(obligation.assumptions)) {
            premise.direct = false;
            evidence = kernel::ProofTerm::implication_introduction(premise.proposition, std::move(evidence));
        }
        obligation.evidence = std::move(evidence);
    }
    program.path_claims.clear();
}

} // namespace

std::optional<kernel::Proposition> rewrite_context(const kernel::Proposition& goal, const kernel::Term& target) {
    return make_rewrite_context(goal, target);
}

std::optional<kernel::Type> detail::core_type(const vir::Type& type) {
    return lower_type(type);
}

std::expected<kernel::Term, detail::Failure> detail::lower_value(const vir::Expr& expression,
                                                                 const DefinitionMap& definitions, std::size_t binders,
                                                                 const CallBindings* calls,
                                                                 const VersionBindings* versions,
                                                                 const OpaqueBindings* opaque) {
    TermLowering lowering(definitions, binders, calls, versions, opaque);
    return lowering.lower(expression);
}

std::expected<kernel::Proposition, detail::Failure> detail::lower_predicate(const vir::Expr& expression,
                                                                            const DefinitionMap& definitions,
                                                                            std::size_t binders) {
    return lower_proposition(expression, definitions, binders);
}

ObligationId detail::identify_goal(const kernel::Context& context, const std::string& subject,
                                   const kernel::Proposition& goal) {
    return identify(context, subject, goal);
}

ObligationId identify_impossibility(Origin origin, const kernel::Context& context, const std::string& subject,
                                    const kernel::Proposition& goal, std::uint64_t position) {
    source::Hasher hasher;
    hasher.update_field("impossibility-v1");
    hasher.update_field(describe(origin));
    hasher.update_field(identify(context, subject, goal).digest.to_short_hex(64));
    hasher.update_u64(position);
    return ObligationId{hasher.finish()};
}

Program generate(const vir::Module& module, const elaboration::Result& elaborated, diagnostics::Engine& engine) {
    Program program;
    DefinitionMap definitions;
    std::map<std::string, Failure> deferred;

    // Definitions are admitted in dependency order. The kernel only accepts a
    // definition whose callees are already present, which is what keeps the
    // definition graph acyclic and normalization terminating.
    std::vector<const vir::Function*> pending;
    for (const vir::Function& function : module.functions) {
        if (function.contract.has_value() && !function.contract->preconditions.empty()) {
            deferred.emplace(
                function.symbol.usr,
                Failure{"call-site preconditions require a verified caller body", function.range.begin, {}});
            continue;
        }
        if (function.purity == vir::Purity::Pure && function.returned_value.has_value()) {
            pending.push_back(&function);
        }
    }

    std::uint32_t next_definition = 0;
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto candidate = pending.begin(); candidate != pending.end();) {
            const vir::Function& function = **candidate;

            std::set<std::string> callees;
            collect_callees(*function.returned_value, callees);
            const bool ready =
                std::ranges::all_of(callees, [&](const std::string& callee) { return definitions.contains(callee); });
            if (!ready) {
                ++candidate;
                continue;
            }

            TermLowering lowering(definitions, function.parameters.size());
            std::expected<kernel::Term, Failure> body = lowering.lower(*function.returned_value);
            if (!body) {
                deferred.emplace(function.symbol.usr, body.error());
                candidate = pending.erase(candidate);
                progress = true;
                continue;
            }

            kernel::Definition definition;
            definition.id = kernel::DefId{next_definition};
            definition.name = function.qualified_name;
            const std::optional<kernel::Type> result_type = lower_type(function.result);
            if (!result_type.has_value()) {
                deferred.emplace(function.symbol.usr,
                                 Failure{"the result type has no core representation", function.range.begin, {}});
                candidate = pending.erase(candidate);
                progress = true;
                continue;
            }
            definition.result = *result_type;
            for (const vir::Parameter& parameter : function.parameters) {
                const std::optional<kernel::Type> type = lower_type(parameter.type);
                if (!type.has_value()) {
                    definition.parameters.clear();
                    break;
                }
                definition.parameters.push_back(*type);
            }

            if (definition.parameters.size() != function.parameters.size()) {
                deferred.emplace(function.symbol.usr,
                                 Failure{"a parameter type has no core representation", function.range.begin, {}});
                candidate = pending.erase(candidate);
                progress = true;
                continue;
            }

            definition.body = std::move(*body);
            if (auto admitted = program.context.define(std::move(definition)); !admitted) {
                deferred.emplace(function.symbol.usr,
                                 Failure{kernel::describe(admitted.error().kind) + ": " + admitted.error().detail,
                                         function.range.begin,
                                         {}});
            } else {
                definitions.emplace(function.symbol.usr, kernel::DefId{next_definition});
                ++next_definition;
            }

            candidate = pending.erase(candidate);
            progress = true;
        }
    }

    for (const vir::Function* function : pending) {
        deferred.emplace(function->symbol.usr, Failure{"its definition is recursive, or depends on a definition that "
                                                       "could not be admitted",
                                                       function->range.begin,
                                                       {}});
    }

    // Why a definition a law reaches for is not available to the core.
    const auto explain = [&elaborated, &deferred](const Failure& failure) {
        std::string note;
        if (failure.missing_symbol.empty()) {
            return note;
        }
        if (const elaboration::FunctionRejection* rejection =
                elaborated.rejection(vir::SymbolId{failure.missing_symbol})) {
            note = "'" + rejection->name + "' was not admitted because " + rejection->reason;
        } else if (const auto deferral = deferred.find(failure.missing_symbol); deferral != deferred.end()) {
            note = deferral->second.reason;
        } else {
            note = "it is not marked pure, so it is not a definition the formal core may unfold";
        }
        return note;
    };

    for (const vir::Law& law : module.laws) {
        std::expected<kernel::Proposition, Failure> conclusion =
            lower_proposition(law.proposition, definitions, law.parameters.size());
        if (!conclusion) {
            report(engine, diagnostics::Category::UnsupportedSemantics,
                   conclusion.error().location.is_valid() ? conclusion.error().location : law.proposition_range.begin,
                   "law '" + law.name + "' cannot be stated to the formal core: " + conclusion.error().reason,
                   explain(conclusion.error()));
            continue;
        }

        kernel::Proposition goal = std::move(*conclusion);

        // A precondition asserts nothing. It is what the conclusion is stated
        // under, so a law that has one states the implication between them
        // (GRAMMAR.md 3).
        if (law.premise.has_value()) {
            std::expected<kernel::Proposition, Failure> premise =
                lower_proposition(*law.premise, definitions, law.parameters.size());
            if (!premise) {
                report(engine, diagnostics::Category::UnsupportedSemantics,
                       premise.error().location.is_valid() ? premise.error().location : law.premise_range.begin,
                       "the precondition of law '" + law.name +
                           "' cannot be stated to the formal core: " + premise.error().reason,
                       explain(premise.error()));
                continue;
            }
            goal = kernel::Proposition::implication(std::move(*premise), std::move(goal));
        }

        std::string unrepresented;
        std::optional<kernel::Proposition> quantified = quantify_over(law.parameters, std::move(goal), unrepresented);
        if (!quantified.has_value()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, law.range.begin,
                   "law '" + law.name + "' quantifies over '" + unrepresented +
                       "', which the formal core does not represent");
            continue;
        }
        goal = std::move(*quantified);

        Obligation obligation;
        obligation.law = law.id;
        obligation.subject = law.name;
        obligation.origin = Origin::LawProposition;
        obligation.range = law.range;
        obligation.trusted = law.trusted;
        obligation.id = identify(program.context, law.name, goal);
        obligation.goal = std::move(goal);
        program.obligations.push_back(std::move(obligation));
    }

    // A trusted law admitting a memory proposition is an explicit assumption
    // with no kernel proposition, so it is recorded with an identity derived
    // from what it states rather than as an obligation (SPEC.md TRUSTED-003,
    // TRUST.md TCB-TRUST-005). The pointer is identified by its position among
    // the law's parameters. A premise or a count is hashed as written, so
    // renaming a parameter they mention yields a new identity: that errs
    // toward invalidating an assumption, never toward keeping a changed one
    // (TCB-TRUST-006).
    for (const vir::MemoryAssumption& assumption : module.memory_assumptions) {
        source::Hasher hasher;
        hasher.update_field("trusted-memory-proposition-v1");
        hasher.update_field(assumption.name);
        for (const vir::Parameter& parameter : assumption.parameters) {
            hasher.update_field(vir::describe(parameter.type));
        }
        hasher.update_field(assumption.premise.has_value() ? vir::describe(*assumption.premise) : std::string{});
        std::string statement;
        for (const vir::Capability& capability : assumption.capabilities) {
            hasher.update_field(vir::describe(capability.kind));
            hasher.update_field(std::to_string(capability.place.root.id));
            hasher.update_field(capability.extent.empty() ? std::string{} : vir::describe(capability.extent.front()));
            statement += (statement.empty() ? "" : " && ") + vir::describe(capability);
        }
        program.memory_assumptions.push_back(TrustedMemoryAssumption{assumption.name, ObligationId{hasher.finish()},
                                                                     assumption.range.begin, std::move(statement)});
    }

    // A refinement type's predicate, stated once so every site a value enters
    // that type can ask for it (SPEC.md 17.2). It is a proposition over the
    // declaration's indices and the value, and it becomes an obligation only
    // where a value actually enters the type - a declaration asserts nothing.
    for (const auto& refinement : module.refinements) {
        RefinementPredicate stated;
        stated.name = refinement.name;
        stated.identity = refinement.identity;
        bool modeled = true;
        for (const auto& index : refinement.indices) {
            const std::optional<kernel::Type> type = detail::core_type(index.type);
            if (!type.has_value()) {
                report(engine, diagnostics::Category::UnsupportedSemantics, refinement.range.begin,
                       "refinement type '" + refinement.name + "' has an index of type '" + vir::describe(index.type) +
                           "', which the formal core does not represent");
                modeled = false;
                break;
            }
            stated.parameters.push_back(*type);
        }
        if (!modeled) {
            continue;
        }
        const std::optional<kernel::Type> base = detail::core_type(refinement.base);
        if (!base.has_value()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, refinement.range.begin,
                   "refinement type '" + refinement.name + "' refines '" + vir::describe(refinement.base) +
                       "', which the formal core does not represent");
            continue;
        }
        stated.parameters.push_back(*base);

        auto predicate = lower_proposition(refinement.predicate, definitions, stated.parameters.size());
        if (!predicate) {
            report(engine, diagnostics::Category::UnsupportedSemantics, predicate.error().location,
                   "refinement type '" + refinement.name +
                       "' cannot be stated to the formal core: " + predicate.error().reason,
                   explain(predicate.error()));
            continue;
        }
        stated.predicate = std::move(*predicate);
        program.refinements.push_back(std::move(stated));
    }

    detail::generate_contracts(module, definitions, program, engine, explain);

    // Direct proves (P) declarations have their own obligations. They never
    // become synthetic Laws or enter the automatic-proof fallback path.
    for (const auto& proof : module.proofs) {
        if (proof.law)
            continue;
        auto proposition = lower_proposition(proof.proposition, definitions, proof.parameters.size());
        if (!proposition) {
            report(engine, diagnostics::Category::UnsupportedSemantics, proposition.error().location,
                   "proof '" + proof.name + "' cannot be stated to the formal core: " + proposition.error().reason,
                   explain(proposition.error()));
            continue;
        }
        std::string unrepresented;
        auto goal = quantify_over(proof.parameters, std::move(*proposition), unrepresented);
        if (!goal) {
            report(engine, diagnostics::Category::UnsupportedSemantics, proof.range.begin,
                   "proof '" + proof.name + "' quantifies over an unsupported type: " + unrepresented);
            continue;
        }
        Obligation obligation;
        obligation.proof = proof.id;
        obligation.subject = proof.name;
        obligation.origin = Origin::ProofProposition;
        obligation.range = proof.range;
        obligation.id = identify(program.context, "proof:" + proof.name, *goal);
        obligation.goal = std::move(*goal);
        program.obligations.push_back(std::move(obligation));
    }

    lower_proofs(module, elaborated, definitions, program, engine);
    discharge_path_claims(program, engine);
    return program;
}

} // namespace cppl::obligations
