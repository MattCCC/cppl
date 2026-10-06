#pragma once

#include "cppl/clang/ast.hpp"
#include "places.hpp"
#include "signature.hpp"

#include <clang-c/Index.h>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <vector>

// The object a member function call is made on (call_objects.cpp, where each is
// documented).
namespace cppl::clangbridge::detail {

// The object a member function call is made on, as a place the caller can form
// (SPEC.md CLASS-011): the declaration it is rooted in -- the class of the
// caller's own implicit object, for `this`, and the pointer, for an object a
// pointer designates -- and the path of members and elements from there to
// the object.
struct CallObject {
    CXCursor declaration = clang_getNullCursor();
    std::vector<PlaceStep> path;
    bool receiver = false;
    // The object is the one pointer `declaration` designates, `p->f()` or
    // `(*p).f()`: its places are dereference places, reached only under the
    // capability the contract states for the pointer (SPEC.md VERIFIED-038).
    bool through_pointer = false;
};

bool calls_through_member_pointer(CXCursor call);

bool is_prvalue(CXCursor expression);

CXCursor designated_object(CXCursor expression);

std::expected<CallObject, std::string> call_object(CXCursor call, CXCursor callee);

std::optional<std::size_t> object_place(const Locals& locals, const std::vector<CXCursor>& parameters,
                                        const CallObject& object, const std::vector<PlaceStep>& path);

Expr receiver_argument(const CallObject& object, const std::vector<PlaceStep>& path, const ReceiverLeaf& leaf,
                       const Signature& signature, const Locals& locals, CXCursor at);

} // namespace cppl::clangbridge::detail
