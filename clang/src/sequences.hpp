#pragma once

#include "cppl/clang/ast.hpp"
#include "cppl/source/representation.hpp"
#include "places.hpp"
#include "signature.hpp"

#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

// A call of a modeled standard sequence or of `std::array`, and where the
// elements a subscript of one select live (RFC 0020).
namespace cppl::clangbridge::detail {

// A call of a member function or constructor of a modeled sequence or of
// `std::array`, decoded from the call Clang resolved (RFC 0020 §6).
//
// Which operation it is comes from the resolved method: its class is the
// specialization, its spelling the member, its parameters the overload. An
// operator is a call whose object is its first argument; any other member
// call names its method through a member reference whose object is the one
// operated on.
struct SequenceCall {
    CXCursor method = clang_getNullCursor();
    CXCursor object = clang_getNullCursor(); // null for a constructor
    std::vector<CXCursor> arguments;
    source::RepresentationKind family = source::RepresentationKind::None;
    std::string name;
    bool constructor = false;
};

// Where the elements a subscript of a modeled sequence select live, and what
// bounds the index (RFC 0020 §3).
//
// A vector's or string's elements are places rooted in the container itself. A
// span local's are the places of the container it views, so `s[i]` and `v[i]`
// at one index term are one place; its own length still bounds the index. A span
// parameter's are places of caller storage, reachable only under a capability.
struct ElementRegion {
    CXCursor declaration = clang_getNullCursor(); // what the element places are rooted in
    std::optional<std::size_t> root;              // the sequence owning them, when this body tracks it
    std::optional<std::size_t> accessed;          // the root of the object subscripted
    std::optional<std::uint32_t> parameter;       // a span parameter, by position
    Type element;
    bool external = false;
    source::RepresentationKind family = source::RepresentationKind::None;
};

std::optional<SequenceCall> sequence_call(CXCursor cursor);

std::string library_name(source::RepresentationKind family, const std::string& member);

Expr element_observation(Expr subject, CXCursor index_cursor, const Signature& signature, const Locals& locals,
                         unsigned depth, CXCursor cursor);

std::expected<ElementRegion, std::string> element_region(CXCursor object, const Locals& locals,
                                                         const Signature& signature);

Expr region_length(const ElementRegion& region, const Locals& locals, CXCursor at);

std::optional<Expr> element_index(const ResolvedAccess& access, const Type& length, const Signature& signature,
                                  const Locals& locals);

std::optional<std::size_t> find_element(const Locals& locals, const ElementRegion& region,
                                        const std::vector<PlaceStep>& path, const Expr& index);

Expr library_call(source::LibraryCall library, const Type& container, std::string name, std::vector<Expr> arguments,
                  Type result, CXCursor at);

std::optional<CXCursor> moved_operand_of(CXCursor cursor);

std::optional<std::size_t> owning_root(CXCursor object, const Locals& locals);

bool is_mutator(const SequenceCall& call);

Expr sequence_expression(const SequenceCall& call, CXCursor cursor, const Signature& signature, const Locals& locals,
                         unsigned depth);

} // namespace cppl::clangbridge::detail
