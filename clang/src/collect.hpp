#pragma once

#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"

#include <clang-c/Index.h>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// Which declarations of a unit are verified (collect.cpp, where each is
// documented).
namespace cppl::clangbridge::detail {

struct RefinedTemplateArgument {
    std::string refinement;
    std::string template_name;
};

struct Collector {
    const Selection* selection = nullptr;
    std::vector<CXCursor> selected;
    // Uses of a verified template's specialization at a refined argument.
    std::vector<std::pair<std::string, CXCursor>> refused_arguments;
    // Specializations of verified function templates this unit instantiated,
    // deduplicated by USR.
    std::vector<CXCursor> specializations;
    std::vector<CXCursor> functions;
    std::vector<CXCursor> unverified_storage;
};

std::vector<TemplateArgument> template_arguments_of(CXCursor cursor);

std::optional<CXCursor> specialized_template(CXCursor cursor);

std::string refined_template_argument_refusal(const RefinedTemplateArgument& refined);

CXChildVisitResult collect(CXCursor cursor, CXCursor, CXClientData data);

CXChildVisitResult collect_specializations(CXCursor cursor, CXCursor, CXClientData data);

std::optional<std::string> refinement_use(CXCursor declaration, const Selection& selection, unsigned depth = 0,
                                          std::unordered_set<std::size_t>* visited = nullptr);

std::optional<RefinedTemplateArgument> refined_template_argument(CXCursor cursor, const Selection& selection,
                                                                 unsigned depth = 0);

} // namespace cppl::clangbridge::detail
