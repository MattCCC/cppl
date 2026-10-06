#include "aggregates.hpp"

#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "lowering.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations::detail {

namespace {

std::unexpected<Failure> fail(std::string reason, const source::SourceLocation& location) {
    return std::unexpected(Failure{std::move(reason), location, {}});
}

// What `subject`, a value of `domain` stated under the fresh binder, is supposed
// to hold when it was assembled as `aggregate`: for each member in order, that
// its projection is the member value, following a member that is itself
// assembled into its own members. Every leaf is a scalar, so each supposition is
// an equality the arithmetic decides with, and none equates two struct values.
// Each operand must stand for the member at its position, with that member's
// type: one supposed of the wrong member would be a false fact.
std::expected<void, Failure> leaf_equations(const vir::Expr& aggregate, const kernel::Type& domain,
                                            const kernel::Term& subject, const MemberLowering& lower,
                                            std::vector<kernel::Proposition>& supposed) {
    const source::SourceLocation& location = aggregate.provenance.range.begin;
    const auto& members = std::get<vir::Aggregate>(aggregate.node).operands;
    const auto& signature = std::get<kernel::ValueType>(domain.node).projections;
    if (members.size() != signature.size()) {
        return fail("an assembled struct value has " + std::to_string(members.size()) + " values for " +
                        std::to_string(signature.size()) + " members",
                    location);
    }
    for (std::size_t index = 0; index < signature.size(); ++index) {
        const vir::Expr& member = members[index];
        const std::optional<kernel::Type> type = core_type(member.type);
        if (!type.has_value() || !(*type == signature[index])) {
            return fail("a member value of an assembled struct value has another type than the member it stands for",
                        location);
        }
        kernel::Term projected = kernel::Term::project(domain, static_cast<std::uint32_t>(index), subject);
        if (std::holds_alternative<vir::Aggregate>(member.node)) {
            if (!type->is_value()) {
                return fail("a member assembled from members of its own is not a struct value", location);
            }
            if (auto nested = leaf_equations(member, *type, projected, lower, supposed); !nested) {
                return nested;
            }
            continue;
        }
        if (type->is_value()) {
            return fail("a member of an assembled struct value is itself a struct value that was not assembled from "
                        "its members",
                        location);
        }
        auto value = lower(member);
        if (!value) {
            return std::unexpected(value.error());
        }
        supposed.push_back(kernel::Proposition::equality(*type, std::move(projected), kernel::shift(*value, 1)));
    }
    return {};
}

} // namespace

// Every node's children are visited, so a node kind added later is searched
// without being listed here.
void collect_aggregates(const vir::Expr& expression, std::vector<const vir::Expr*>& sites) {
    if (std::holds_alternative<vir::Aggregate>(expression.node)) {
        sites.push_back(&expression);
        return;
    }
    std::visit(
        [&sites](const auto& node) {
            if constexpr (requires { node.operands; }) {
                for (const vir::Expr& child : node.operands)
                    collect_aggregates(child, sites);
            }
            if constexpr (requires { node.arguments; }) {
                for (const vir::Expr& child : node.arguments)
                    collect_aggregates(child, sites);
            }
            if constexpr (requires { node.extent; }) {
                for (const vir::Expr& child : node.extent)
                    collect_aggregates(child, sites);
            }
            if constexpr (requires { node.body; }) {
                for (const vir::Expr& child : node.body)
                    collect_aggregates(child, sites);
            }
        },
        expression.node);
}

bool contains_aggregate(const vir::Expr& expression) {
    std::vector<const vir::Expr*> sites;
    collect_aggregates(expression, sites);
    return !sites.empty();
}

// That supposes only that an object of the type with those member values
// exists, which holds of every type the bridge assembles: a record or an array
// whose every member is a modeled scalar, record or array.
std::expected<void, Failure> bind_assembled(const vir::Expr& expression, const CallBindings& bound,
                                            const MemberLowering& lower, const BindAssembled& bind) {
    std::vector<const vir::Expr*> sites;
    collect_aggregates(expression, sites);
    for (const vir::Expr* site : sites) {
        if (bound.contains(site->id.value)) {
            continue;
        }
        const std::optional<kernel::Type> type = core_type(site->type);
        if (!type.has_value() || !type->is_value()) {
            return fail("an assembled struct value has a type the formal core does not represent as a value",
                        site->provenance.range.begin);
        }
        // Stated over the scope before the fresh value, then supposed after it,
        // where the value is the innermost binder.
        std::vector<kernel::Proposition> supposed;
        if (auto stated = leaf_equations(*site, *type, kernel::Term::variable(kernel::VarIndex{0}), lower, supposed);
            !stated) {
            return stated;
        }
        bind(*site, *type, std::move(supposed));
    }
    return {};
}

} // namespace cppl::obligations::detail
