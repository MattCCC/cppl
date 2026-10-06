// The canonical identity of a statement: its encoding, and the definitions
// it depends on.

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/kernel/version.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/digest.hpp"
#include "generate_detail.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations {

namespace {

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

} // namespace

namespace detail::generation {

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

} // namespace detail::generation

} // namespace cppl::obligations
