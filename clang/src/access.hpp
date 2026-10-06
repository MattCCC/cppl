#pragma once

#include "cppl/clang/ast.hpp"
#include "cppl/source/storage.hpp"
#include "places.hpp"
#include "signature.hpp"

#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// How an access names storage (access.cpp, where each is documented).
namespace cppl::clangbridge::detail {

Place place_of(const Locals& locals, std::size_t entry);

CXType reference_value_type(CXType written);

source::ParameterPassing passing_of(CXType written);

bool may_write_through(CXType written);

bool designates_storage(CXType written);

bool generation_current(const Locals& locals, const Local& entry);

std::optional<std::size_t> find_binding(const Locals& locals, CXCursor declaration,
                                        const std::vector<PlaceStep>& path = {}, const Expr* index_term = nullptr);

std::optional<std::string> stale_borrow(const Locals& locals, std::size_t binding);

std::optional<std::size_t> find_local(const Locals& locals, CXCursor declaration,
                                      const std::vector<PlaceStep>& path = {}, const Expr* index_term = nullptr);

CXCursor enclosing_record(CXCursor member);

std::optional<std::uint32_t> constant_index_of(CXCursor subscript);

std::optional<CXCursor> this_record(CXCursor cursor);

std::optional<ResolvedAccess> resolve_access(CXCursor cursor);

bool same_term(const Expr& lhs, const Expr& rhs);

std::optional<std::size_t> find_deref(const Locals& locals, std::size_t pointer, std::uint32_t version,
                                      const std::vector<PlaceStep>& path, const Expr* index_term);

std::optional<std::size_t> tracked_place(CXCursor cursor, const Locals& locals, const Signature& signature);

std::optional<std::pair<std::size_t, std::uint32_t>> pointer_root(const Locals& locals,
                                                                  const std::vector<CXCursor>& parameters,
                                                                  CXCursor pointer);

std::optional<std::size_t> pointee_place(const Locals& locals, const std::vector<CXCursor>& parameters,
                                         CXCursor pointer, const std::vector<PlaceStep>& path);

Expr read_place(const Locals& locals, std::size_t entry, CXCursor at);

} // namespace cppl::clangbridge::detail
