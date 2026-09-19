#pragma once

#include "cppl/obligations/generate.hpp"

#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace cppl::obligations::detail {

struct Failure {
    std::string reason;
    source::SourceLocation location;
    std::string missing_symbol;
};

using DefinitionMap = std::map<std::string, kernel::DefId>;
using CallBindings = std::map<std::uint32_t, std::size_t>;
// The value each logical version of a local denotes, for the versions a path
// has established before the expression being lowered.
using VersionBindings = std::map<std::uint32_t, const vir::Expr*>;

std::optional<kernel::Type> core_type(const vir::Type& type);
std::expected<kernel::Term, Failure> lower_value(const vir::Expr& expression, const DefinitionMap& definitions,
                                                 std::size_t binders, const CallBindings* calls = nullptr,
                                                 const VersionBindings* versions = nullptr);
std::expected<kernel::Proposition, Failure> lower_predicate(const vir::Expr& expression,
                                                            const DefinitionMap& definitions, std::size_t binders);
ObligationId identify_goal(const kernel::Context& context, const std::string& subject, const kernel::Proposition& goal);
void generate_contracts(const vir::Module& module, const DefinitionMap& pure_definitions, Program& program,
                        diagnostics::Engine& engine, const std::function<std::string(const Failure&)>& explain);

} // namespace cppl::obligations::detail
