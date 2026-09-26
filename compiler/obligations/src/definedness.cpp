#include "definedness.hpp"

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/types.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations::detail {

namespace {

using Guards = std::vector<std::pair<const vir::Expr*, bool>>;

bool is_signed_integer(const vir::Type& type) {
    return type.is_integer() && type.integer_type().is_signed;
}

kernel::Wide least(const vir::IntType& type) {
    return type.is_signed ? -(kernel::Wide{1} << (type.width - 1)) : kernel::Wide{0};
}

kernel::Wide greatest(const vir::IntType& type) {
    return type.is_signed ? (kernel::Wide{1} << (type.width - 1)) - 1 : (kernel::Wide{1} << type.width) - 1;
}

// Whether converting `from` to `to` may produce a value `to` does not hold,
// where C++ does not define the result as reduction: a signed target.
bool conversion_needs_room(const vir::Type& to, const vir::Type& from) {
    if (!to.is_integer() || !from.is_integer() || !to.integer_type().is_signed) {
        return false;
    }
    return least(from.integer_type()) < least(to.integer_type()) ||
           greatest(from.integer_type()) > greatest(to.integer_type());
}

// The conditions `expression` itself owes as an operation, apart from its
// operands'. Appended in the order they are stated.
void own_sites(const vir::Expr& expression, const Guards& guards, std::vector<DefinednessSite>& sites) {
    const auto site = [&](Definedness kind) {
        sites.push_back(DefinednessSite{&expression, kind, guards});
    };
    if (const auto* binary = std::get_if<vir::Binary>(&expression.node)) {
        switch (binary->op) {
            case vir::BinaryOp::Add:
            case vir::BinaryOp::Sub:
            case vir::BinaryOp::Mul:
                if (is_signed_integer(expression.type)) {
                    site(Definedness::SignedOverflow);
                }
                return;
            case vir::BinaryOp::Div:
            case vir::BinaryOp::Rem:
                if (expression.type.is_integer()) {
                    site(Definedness::ZeroDivisor);
                    if (is_signed_integer(expression.type)) {
                        site(Definedness::QuotientOverflow);
                    }
                }
                return;
            case vir::BinaryOp::Equal:
            case vir::BinaryOp::NotEqual:
            case vir::BinaryOp::Less:
            case vir::BinaryOp::LessEqual:
            case vir::BinaryOp::Greater:
            case vir::BinaryOp::GreaterEqual:
            case vir::BinaryOp::And:
            case vir::BinaryOp::Or:
                return;
        }
        return;
    }
    if (std::holds_alternative<vir::Minus>(expression.node)) {
        if (is_signed_integer(expression.type)) {
            site(Definedness::SignedOverflow);
        }
        return;
    }
    if (const auto* conversion = std::get_if<vir::Conversion>(&expression.node)) {
        if (conversion->operands.size() == 1 && conversion_needs_room(expression.type, conversion->operands[0].type)) {
            site(Definedness::Conversion);
        }
    }
}

void collect(const vir::Expr& expression, Guards& guards, std::vector<DefinednessSite>& sites) {
    // `?:` evaluates one arm, selected by its condition, so an operation in an
    // arm is guarded by the condition's outcome. `&&` and `||` never stand
    // here as values: a condition a path is taken on is already split into
    // the routes they select (BOUNDARYEX-001), a specification states them as
    // connectives whose operands are each specified, and anywhere else they
    // are refused. Were one to reach here, its operands would owe their
    // conditions unguarded, which asks more, never less.
    if (const auto* choice = std::get_if<vir::Conditional>(&expression.node)) {
        if (choice->operands.size() == 3) {
            collect(choice->operands[0], guards, sites);
            guards.emplace_back(&choice->operands[0], true);
            collect(choice->operands[1], guards, sites);
            guards.back().second = false;
            collect(choice->operands[2], guards, sites);
            guards.pop_back();
        }
        return;
    }
    // A subscript's bound wraps the observation it bounds, which already
    // holds the index; the walk evaluates a statement-level bound's own
    // operands where it stands.
    if (const auto* bounded = std::get_if<vir::ElementBound>(&expression.node)) {
        if (bounded->operands.size() == 2) {
            collect(bounded->operands[1], guards, sites);
        }
        return;
    }
    const bool value = std::visit(
        [](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            return std::is_same_v<Node, vir::Binary> || std::is_same_v<Node, vir::Negation> ||
                   std::is_same_v<Node, vir::Minus> || std::is_same_v<Node, vir::Conversion> ||
                   std::is_same_v<Node, vir::Call> || std::is_same_v<Node, vir::Projection> ||
                   std::is_same_v<Node, vir::Element> || std::is_same_v<Node, vir::FormalEquality>;
        },
        expression.node);
    if (!value) {
        return;
    }
    std::visit(
        [&](const auto& node) {
            if constexpr (requires { node.operands; }) {
                for (const vir::Expr& operand : node.operands) {
                    collect(operand, guards, sites);
                }
            } else if constexpr (requires { node.arguments; }) {
                for (const vir::Expr& argument : node.arguments) {
                    collect(argument, guards, sites);
                }
            }
        },
        expression.node);
    own_sites(expression, guards, sites);
}

bool contains(const vir::Expr& root, const vir::Expr& target) {
    if (&root == &target) {
        return true;
    }
    return std::visit(
        [&](const auto& node) {
            if constexpr (requires { node.operands; }) {
                for (const vir::Expr& operand : node.operands) {
                    if (contains(operand, target)) {
                        return true;
                    }
                }
            } else if constexpr (requires { node.arguments; }) {
                for (const vir::Expr& argument : node.arguments) {
                    if (contains(argument, target)) {
                        return true;
                    }
                }
            }
            return false;
        },
        root.node);
}

// The operation as written, without the parentheses a binary operation's
// description wraps itself in.
std::string written(const vir::Expr& expression) {
    std::string text = vir::describe(expression);
    if (std::holds_alternative<vir::Binary>(expression.node) && text.size() >= 2 && text.front() == '(' &&
        text.back() == ')') {
        text = text.substr(1, text.size() - 2);
    }
    return text;
}

std::string operation_name(const vir::Expr& expression) {
    if (const auto* binary = std::get_if<vir::Binary>(&expression.node)) {
        switch (binary->op) {
            case vir::BinaryOp::Add:
                return "addition";
            case vir::BinaryOp::Sub:
                return "subtraction";
            case vir::BinaryOp::Mul:
                return "multiplication";
            default:
                break;
        }
    }
    return "negation";
}

std::unexpected<Failure> malformed(const vir::Expr& expression) {
    return std::unexpected(
        Failure{"malformed operation '" + written(expression) + "'", expression.provenance.range.begin, {}});
}

kernel::Proposition holds(kernel::PrimOp op, const kernel::IntType& type, kernel::Term lhs, kernel::Term rhs) {
    return kernel::predicate(kernel::Term::primitive(op, type, {std::move(lhs), std::move(rhs)}), true);
}

// Whether the kernel's own normalization decides the condition true: every
// equality it states has sides of one normal form. Such a condition adds
// nothing to a specification it is conjoined into, so it is left out, which
// keeps a condition on constants, `x / 2` owing `2 != 0`, from cluttering what
// the specification states. A term naming a definition is not normalized here
// and so is never judged true.
bool evidently_true(const kernel::Proposition& proposition) {
    if (const auto* equality = std::get_if<kernel::Eq>(&proposition.node)) {
        const kernel::Context none;
        const auto lhs = kernel::normalize(none, equality->lhs);
        const auto rhs = kernel::normalize(none, equality->rhs);
        return lhs && rhs && *lhs == *rhs;
    }
    if (const auto* conjunction = std::get_if<kernel::And>(&proposition.node)) {
        return evidently_true(*conjunction->left) && evidently_true(*conjunction->right);
    }
    if (const auto* implication = std::get_if<kernel::Implies>(&proposition.node)) {
        return evidently_true(*implication->conclusion);
    }
    return false;
}

} // namespace

std::vector<DefinednessSite> definedness_sites(const vir::Expr& expression) {
    std::vector<DefinednessSite> sites;
    Guards guards;
    collect(expression, guards, sites);
    return sites;
}

std::optional<DefinednessSite> first_definedness_site(const vir::Expr& body) {
    std::vector<DefinednessSite> sites;
    own_sites(body, {}, sites);
    if (!sites.empty()) {
        return sites.front();
    }
    return std::visit(
        [](const auto& node) {
            std::optional<DefinednessSite> found;
            const auto search = [&found](const std::vector<vir::Expr>& children) {
                for (const vir::Expr& child : children) {
                    if (!found) {
                        found = first_definedness_site(child);
                    }
                }
            };
            if constexpr (requires { node.operands; }) {
                search(node.operands);
            }
            if constexpr (requires { node.arguments; }) {
                search(node.arguments);
            }
            if constexpr (requires { node.extent; }) {
                search(node.extent);
            }
            if constexpr (requires { node.body; }) {
                search(node.body);
            }
            return found;
        },
        body.node);
}

bool sequenced_before(const DefinednessSite& site, const vir::Expr& call) {
    if (site.operation != nullptr && contains(*site.operation, call)) {
        return true;
    }
    for (const auto& [guard, taken] : site.guards) {
        if (contains(*guard, call)) {
            return true;
        }
    }
    return false;
}

std::expected<kernel::Proposition, Failure> definedness_condition(const DefinednessSite& site,
                                                                  const TermLowerer& lower) {
    const vir::Expr& operation = *site.operation;
    const std::optional<kernel::Type> type = core_type(operation.type);
    if (!type || !type->is_integer()) {
        return malformed(operation);
    }
    const kernel::IntType integer = type->integer_type();
    const auto lowered = [&](std::size_t index) -> std::expected<kernel::Term, Failure> {
        return std::visit(
            [&](const auto& node) -> std::expected<kernel::Term, Failure> {
                if constexpr (requires { node.operands; }) {
                    if (index < node.operands.size()) {
                        return lower(node.operands[index]);
                    }
                }
                return malformed(operation);
            },
            operation.node);
    };

    std::optional<kernel::Proposition> condition;
    switch (site.kind) {
        case Definedness::SignedOverflow: {
            if (std::holds_alternative<vir::Minus>(operation.node)) {
                auto operand = lowered(0);
                if (!operand) {
                    return std::unexpected(operand.error());
                }
                condition =
                    holds(kernel::PrimOp::SubFits, integer, kernel::Term::literal(integer, 0), std::move(*operand));
                break;
            }
            const auto* binary = std::get_if<vir::Binary>(&operation.node);
            if (binary == nullptr) {
                return malformed(operation);
            }
            auto lhs = lowered(0);
            auto rhs = lowered(1);
            if (!lhs || !rhs) {
                return std::unexpected(!lhs ? lhs.error() : rhs.error());
            }
            const kernel::PrimOp representable = binary->op == vir::BinaryOp::Add   ? kernel::PrimOp::AddFits
                                                 : binary->op == vir::BinaryOp::Sub ? kernel::PrimOp::SubFits
                                                                                    : kernel::PrimOp::MulFits;
            condition = holds(representable, integer, std::move(*lhs), std::move(*rhs));
            break;
        }
        case Definedness::ZeroDivisor: {
            auto divisor = lowered(1);
            if (!divisor) {
                return std::unexpected(divisor.error());
            }
            condition =
                holds(kernel::PrimOp::NotEqual, integer, std::move(*divisor), kernel::Term::literal(integer, 0));
            break;
        }
        case Definedness::QuotientOverflow: {
            auto dividend = lowered(0);
            auto divisor = lowered(1);
            if (!dividend || !divisor) {
                return std::unexpected(!dividend ? dividend.error() : divisor.error());
            }
            condition = kernel::Proposition::implication(
                holds(kernel::PrimOp::Equal, integer, std::move(*dividend),
                      kernel::Term::literal(integer, least(operation.type.integer_type()))),
                holds(kernel::PrimOp::NotEqual, integer, std::move(*divisor), kernel::Term::literal(integer, -1)));
            break;
        }
        case Definedness::Conversion: {
            const auto* conversion = std::get_if<vir::Conversion>(&operation.node);
            if (conversion == nullptr || conversion->operands.size() != 1) {
                return malformed(operation);
            }
            const vir::Expr& operand = conversion->operands[0];
            const std::optional<kernel::Type> source = core_type(operand.type);
            if (!source || !source->is_integer() || !operand.type.is_integer()) {
                return malformed(operation);
            }
            auto value = lower(operand);
            if (!value) {
                return std::unexpected(value.error());
            }
            // Each side is stated at the source type, and only where the
            // source type reaches past it, so each bound is one of its values.
            const vir::IntType& from = operand.type.integer_type();
            const vir::IntType& to = operation.type.integer_type();
            const kernel::IntType stated = source->integer_type();
            if (least(from) < least(to)) {
                condition = holds(kernel::PrimOp::LessEqual, stated, kernel::Term::literal(stated, least(to)), *value);
            }
            if (greatest(from) > greatest(to)) {
                kernel::Proposition upper =
                    holds(kernel::PrimOp::LessEqual, stated, *value, kernel::Term::literal(stated, greatest(to)));
                condition = condition ? kernel::Proposition::conjunction(std::move(*condition), std::move(upper))
                                      : std::move(upper);
            }
            break;
        }
    }
    if (!condition) {
        return malformed(operation);
    }
    for (const auto& [guard, taken] : std::ranges::reverse_view(site.guards)) {
        auto selected = lower(*guard);
        if (!selected) {
            return std::unexpected(selected.error());
        }
        condition = kernel::Proposition::implication(kernel::predicate(*selected, taken), std::move(*condition));
    }
    return std::move(*condition);
}

std::vector<std::string> explain(const DefinednessSite& site) {
    const vir::Expr& operation = *site.operation;
    const std::string type = vir::spelled(operation.type);
    switch (site.kind) {
        case Definedness::SignedOverflow:
            return {"signed overflow: '" + written(operation) + "' on the signed type '" + type +
                        "' is not shown to stay within '" + type + "'",
                    "C++ leaves a signed " + operation_name(operation) +
                        " undefined unless its exact result is a value of its type, so every evaluation owes that "
                        "it is (SPEC.md ARITH-006, DEFINEDBEHAVIOR-001)"};
        case Definedness::ZeroDivisor: {
            const auto* binary = std::get_if<vir::Binary>(&operation.node);
            const std::string divisor =
                binary != nullptr && binary->operands.size() == 2 ? written(binary->operands[1]) : "the divisor";
            return {"division by zero: the divisor '" + divisor + "' of '" + written(operation) + "' ('" + type +
                        "') is not shown to be nonzero",
                    "C++ leaves '/' and '%' by zero undefined, so every evaluation owes a nonzero divisor (SPEC.md "
                    "ARITH-007, DEFINEDBEHAVIOR-002)"};
        }
        case Definedness::QuotientOverflow:
            return {"signed division overflow: '" + written(operation) + "' on the signed type '" + type +
                        "' is not shown to avoid the least value of '" + type + "' divided by -1",
                    "that quotient is not a value of the type, and C++ leaves both '/' and '%' of the pair "
                    "undefined (SPEC.md ARITH-007, DEFINEDBEHAVIOR-003)"};
        case Definedness::Conversion: {
            const auto* conversion = std::get_if<vir::Conversion>(&operation.node);
            const vir::Expr* operand =
                conversion != nullptr && conversion->operands.size() == 1 ? &conversion->operands[0] : nullptr;
            return {"unrepresentable conversion: '" + (operand != nullptr ? written(*operand) : written(operation)) +
                        "' of type '" + (operand != nullptr ? vir::spelled(operand->type) : type) +
                        "' is not shown to be a value of '" + type + "', to which it is " +
                        (conversion != nullptr && conversion->written ? "cast" : "implicitly converted"),
                    "a value converted to a signed type that cannot hold it is implementation-defined before "
                    "C++20 and reduced since; verification requires it to fit in every mode (SPEC.md ARITH-008)"};
        }
    }
    return {"an operation is not shown to have defined behavior"};
}

std::expected<std::optional<kernel::Proposition>, Failure> definedness_of(const vir::Expr& expression,
                                                                          const TermLowerer& lower) {
    std::optional<kernel::Proposition> defined;
    // One condition stated twice, such as the same conversion of both sides of
    // an equality, is stated once.
    std::vector<kernel::Proposition> stated;
    for (const DefinednessSite& site : definedness_sites(expression)) {
        auto condition = definedness_condition(site, lower);
        if (!condition) {
            return std::unexpected(condition.error());
        }
        if (evidently_true(*condition) || std::ranges::find(stated, *condition) != stated.end()) {
            continue;
        }
        stated.push_back(*condition);
        defined = defined ? kernel::Proposition::conjunction(std::move(*defined), std::move(*condition))
                          : std::move(*condition);
    }
    return defined;
}

std::expected<kernel::Proposition, Failure> specified(const vir::Expr& condition, const TermLowerer& lower) {
    auto term = lower(condition);
    if (!term) {
        return std::unexpected(term.error());
    }
    return specified(condition, kernel::predicate(*term, true), lower);
}

std::expected<kernel::Proposition, Failure> specified(const vir::Expr& expression, kernel::Proposition stated,
                                                      const TermLowerer& lower) {
    auto defined = definedness_of(expression, lower);
    if (!defined) {
        return std::unexpected(defined.error());
    }
    if (!defined->has_value()) {
        return stated;
    }
    return kernel::Proposition::conjunction(std::move(**defined), std::move(stated));
}

} // namespace cppl::obligations::detail
