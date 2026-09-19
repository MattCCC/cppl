#pragma once

#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "cppl/obligations/generate.hpp"

namespace cppl::obligations::detail {

struct Failure {
    std::string reason;
    source::SourceLocation location;
    std::string missing_symbol;
};

using DefinitionMap = std::map<std::string, kernel::DefId>;
using CallBindings = std::map<std::uint32_t, std::size_t>;

std::optional<kernel::Type> core_type(const vir::Type& type);
std::expected<kernel::Term, Failure> lower_value(const vir::Expr& expression,
                                                const DefinitionMap& definitions,
                                                std::size_t binders,
                                                const CallBindings* calls = nullptr);
std::expected<kernel::Proposition, Failure> lower_predicate(const vir::Expr& expression,
                                                         const DefinitionMap& definitions,
                                                         std::size_t binders);
ObligationId identify_goal(const kernel::Context& context, const std::string& subject,
                          const kernel::Proposition& goal);
void generate_contracts(const vir::Module& module, const DefinitionMap& pure_definitions,
                        Program& program, diagnostics::Engine& engine,
                        const std::function<std::string(const Failure&)>& explain);

}  // namespace cppl::obligations::detail
