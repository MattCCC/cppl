#include "unsafe.hpp"

#include "access.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/storage.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "signature.hpp"

#include <algorithm>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Unsafe blocks (SPEC.md 26, TRUST.md TCB-UNSAFE-002 to TCB-UNSAFE-004): where
// the projector marked one, which callees may write through unsafe code what
// they are handed, what a block may change, and a block on a verified body's
// path, which is never lowered.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::strip_parens;
using bridge::take;

namespace {

// The functions a definition calls, by Clang's resolution of each call in it.
// A call Clang leaves overloaded names every candidate.
std::vector<CXCursor> called_functions(CXCursor definition) {
    std::vector<CXCursor> called;
    clang_visitChildren(
        definition,
        [](CXCursor cursor, CXCursor, CXClientData data) {
            if (clang_getCursorKind(cursor) != CXCursor_CallExpr) {
                return CXChildVisit_Recurse;
            }
            auto& found = *static_cast<std::vector<CXCursor>*>(data);
            const CXCursor referenced = clang_getCursorReferenced(cursor);
            if (clang_getCursorKind(referenced) == CXCursor_OverloadedDeclRef) {
                for (unsigned index = 0; index < clang_getNumOverloadedDecls(referenced); ++index) {
                    found.push_back(clang_getOverloadedDecl(referenced, index));
                }
            } else if (clang_Cursor_isNull(referenced) == 0) {
                found.push_back(referenced);
            }
            return CXChildVisit_Recurse;
        },
        &called);
    return called;
}

// How control leaves an unsafe block other than by reaching its end, if it can.
// A `break` or `continue` belonging to a loop or a `switch` inside the block
// stays inside it; a lambda's `return` is the lambda's own.
std::optional<std::string> leaves_block(CXCursor cursor, unsigned loops, unsigned breakable, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return "statements nested too deeply to follow";
    }
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_LambdaExpr) {
        return std::nullopt;
    }
    if (kind == CXCursor_ReturnStmt) {
        return "a return";
    }
    if (kind == CXCursor_GotoStmt || kind == CXCursor_IndirectGotoStmt) {
        return "a goto";
    }
    if (kind == CXCursor_BreakStmt && breakable == 0) {
        return "a break";
    }
    if (kind == CXCursor_ContinueStmt && loops == 0) {
        return "a continue";
    }
    const bool loop = kind == CXCursor_WhileStmt || kind == CXCursor_ForStmt || kind == CXCursor_DoStmt ||
                      kind == CXCursor_CXXForRangeStmt;
    const bool switches = kind == CXCursor_SwitchStmt;
    for (const CXCursor child : children_of(cursor)) {
        if (auto left =
                leaves_block(child, loops + (loop ? 1U : 0U), breakable + (loop || switches ? 1U : 0U), depth + 1)) {
            return left;
        }
    }
    return std::nullopt;
}

// Whether `cursor` names storage of `declaration`: the declaration itself, or a
// member or an element of it, parentheses and value-preserving conversions
// aside. A dereference names what a pointer designates rather than the
// pointer's own storage, so it is not storage of the pointer.
//
// A member is part of its object, so writing one changes what the object holds.
// Asking only whether the declaration is named directly would miss `s.x = 5`,
// and an unsafe block writing a member of a parameter this body does not track
// would leave a stale value standing after it (SPEC.md UNSAFE-005).
bool rooted_in(CXCursor cursor, CXCursor declaration) {
    const auto access = resolve_access(strip_parens(cursor));
    return access.has_value() && !access->dereferenced && clang_equalCursors(access->declaration, declaration) != 0;
}

} // namespace

UnsafeEffects::UnsafeEffects(std::string prefix, const std::vector<std::string>& imported)
    : prefix_(std::move(prefix)),
      imported_(imported.begin(), imported.end()) {}

bool UnsafeEffects::of(CXCursor callee) {
    const std::string key = key_of(callee);
    if (const auto known = known_.find(key); known != known_.end()) {
        return known->second;
    }
    struct Node {
        bool effects = false;
        std::vector<std::string> callees;
    };
    std::map<std::string, Node> nodes;
    std::vector<CXCursor> pending{callee};
    while (!pending.empty()) {
        const CXCursor current = pending.back();
        pending.pop_back();
        const std::string current_key = key_of(current);
        if (nodes.contains(current_key) || known_.contains(current_key)) {
            continue;
        }
        Node& node = nodes[current_key];
        // A function Clang gives no identity cannot be told apart from one
        // that holds an unsafe block.
        if (current_key.empty() || imported_.contains(current_key)) {
            node.effects = true;
            continue;
        }
        const CXCursor definition = clang_getCursorDefinition(current);
        // No C++L construct, an unsafe block included, stands in a system
        // header, and a function this unit does not define is known only by
        // the interface that records it.
        if (prefix_.empty() || clang_Cursor_isNull(definition) != 0 ||
            clang_Location_isInSystemHeader(clang_getCursorLocation(definition)) != 0) {
            continue;
        }
        if (!unsafe_blocks_in(definition, prefix_).empty()) {
            node.effects = true;
            continue;
        }
        for (const CXCursor called : called_functions(definition)) {
            node.callees.push_back(key_of(called));
            pending.push_back(called);
        }
    }
    for (bool grew = true; grew;) {
        grew = false;
        for (auto& entry : nodes) {
            Node& node = entry.second;
            if (node.effects) {
                continue;
            }
            node.effects = std::ranges::any_of(node.callees, [&](const std::string& called) {
                if (const auto found = nodes.find(called); found != nodes.end()) {
                    return found->second.effects;
                }
                const auto known = known_.find(called);
                return known != known_.end() && known->second;
            });
            grew = grew || node.effects;
        }
    }
    for (const auto& entry : nodes) {
        known_.emplace(entry.first, entry.second.effects);
    }
    return known_.at(key);
}

std::string UnsafeEffects::key_of(CXCursor function) {
    return take(clang_getCursorUSR(clang_getCanonicalCursor(function)));
}

// The declaration the projector put just inside an unsafe block's `{`, when
// `statement` is such a block (SPEC.md 26). A nested block has none: it is part
// of the region holding it.
std::optional<CXCursor> unsafe_marker_of(CXCursor statement, const std::string& prefix) {
    if (prefix.empty() || clang_getCursorKind(statement) != CXCursor_CompoundStmt) {
        return std::nullopt;
    }
    const std::vector<CXCursor> children = children_of(statement);
    if (children.empty() || clang_getCursorKind(children.front()) != CXCursor_DeclStmt) {
        return std::nullopt;
    }
    const std::vector<CXCursor> declared = children_of(children.front());
    if (declared.size() != 1 || clang_getCursorKind(declared.front()) != CXCursor_VarDecl ||
        !take(clang_getCursorSpelling(declared.front())).starts_with(prefix + "unsafe_")) {
        return std::nullopt;
    }
    return declared.front();
}

// Every marked unsafe block a subtree holds.
std::vector<CXCursor> unsafe_blocks_in(CXCursor root, const std::string& prefix, unsigned depth) {
    std::vector<CXCursor> found;
    if (depth > kMaxExpressionDepth) {
        return found;
    }
    if (unsafe_marker_of(root, prefix).has_value()) {
        found.push_back(root);
        return found;
    }
    for (const CXCursor child : children_of(root)) {
        std::vector<CXCursor> inner = unsafe_blocks_in(child, prefix, depth + 1);
        found.insert(found.end(), inner.begin(), inner.end());
    }
    return found;
}

bool writes_unsafely(UnsafeEffects* effects, CXCursor callee) {
    if (effects == nullptr) {
        return false;
    }
    const bool member = clang_getCursorKind(callee) == CXCursor_CXXMethod && clang_CXXMethod_isStatic(callee) == 0;
    const bool hands = member || std::ranges::any_of(parameters_of(callee), [](CXCursor parameter) {
                           const CXType declared = clang_getCursorType(parameter);
                           return source::aliases_storage(passing_of(declared)) || designates_storage(declared);
                       });
    return hands && effects->of(callee);
}

// The variables and parameters a subtree names, by Clang's resolution.
std::unordered_set<unsigned> named_declarations(CXCursor root) {
    std::unordered_set<unsigned> named;
    clang_visitChildren(
        root,
        [](CXCursor cursor, CXCursor, CXClientData data) {
            if (clang_getCursorKind(cursor) == CXCursor_DeclRefExpr) {
                const CXCursor declaration = clang_getCursorReferenced(cursor);
                if (clang_getCursorKind(declaration) == CXCursor_VarDecl ||
                    clang_getCursorKind(declaration) == CXCursor_ParmDecl) {
                    static_cast<std::unordered_set<unsigned>*>(data)->insert(clang_hashCursor(declaration));
                }
            }
            return CXChildVisit_Recurse;
        },
        &named);
    return named;
}

// Whether code in `root` may change what `declaration` itself holds, now or
// later: by writing it or a member or element of it, by taking the address of
// any of those, by binding a reference to one that is not const, by calling a
// member function on it, or by capturing it in a lambda. Reading it, passing it
// by value and reaching what it points to leave it as it was.
bool may_rebind(CXCursor root, CXCursor declaration, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return true;
    }
    const CXCursorKind kind = clang_getCursorKind(root);
    const std::vector<CXCursor> children = children_of(root);
    if (kind == CXCursor_LambdaExpr) {
        std::unordered_set<unsigned> captured = named_declarations(root);
        return captured.contains(clang_hashCursor(declaration));
    }
    if (((kind == CXCursor_BinaryOperator && clang_getCursorBinaryOperatorKind(root) == CXBinaryOperator_Assign) ||
         kind == CXCursor_CompoundAssignOperator) &&
        !children.empty() && rooted_in(children.front(), declaration)) {
        return true;
    }
    if (kind == CXCursor_UnaryOperator && children.size() == 1 && rooted_in(children.front(), declaration)) {
        const enum CXUnaryOperatorKind op = clang_getCursorUnaryOperatorKind(root);
        if (op == CXUnaryOperator_PreInc || op == CXUnaryOperator_PostInc || op == CXUnaryOperator_PreDec ||
            op == CXUnaryOperator_PostDec || op == CXUnaryOperator_AddrOf) {
            return true;
        }
    }
    if (kind == CXCursor_VarDecl && source::aliases_storage(passing_of(clang_getCursorType(root))) &&
        passing_of(clang_getCursorType(root)) != source::ParameterPassing::ConstReference) {
        const CXCursor initializer = clang_Cursor_getVarDeclInitializer(root);
        if (clang_Cursor_isNull(initializer) == 0 && rooted_in(initializer, declaration)) {
            return true;
        }
    }
    if (kind == CXCursor_CallExpr) {
        const CXCursor callee = clang_getCursorReferenced(root);
        const std::vector<CXCursor> parameters = parameters_of(callee);
        const int count = clang_Cursor_getNumArguments(root);
        for (int index = 0; index >= 0 && index < count; ++index) {
            const CXCursor argument = clang_Cursor_getArgument(root, static_cast<unsigned>(index));
            const bool by_reference =
                static_cast<std::size_t>(index) < parameters.size() &&
                source::may_write(passing_of(clang_getCursorType(parameters[static_cast<std::size_t>(index)])));
            if (by_reference && rooted_in(argument, declaration)) {
                return true;
            }
        }
        // A member function called on the object may write it through `this`,
        // whatever its qualifiers: a `const` one may still write a `mutable`
        // member (SPEC.md CONTRACT-010).
        if (clang_getCursorKind(callee) == CXCursor_CXXMethod && clang_CXXMethod_isStatic(callee) == 0 &&
            !children.empty() && clang_getCursorKind(children.front()) == CXCursor_MemberRefExpr) {
            const std::vector<CXCursor> object = children_of(children.front());
            if (object.size() == 1 && rooted_in(object.front(), declaration)) {
                return true;
            }
        }
    }
    return std::ranges::any_of(children, [&](CXCursor child) { return may_rebind(child, declaration, depth + 1); });
}

// The places an unsafe block could have written: a pointee, the storage a
// reference parameter designates, and any local whose address this body
// takes or that an unsafe block of this body names. The last set is the
// `escaped` one, which `extract_body` widens by every name an unsafe block
// uses, because such a block may keep an address and write through it later
// (TRUST.md TCB-UNSAFE-002). A reference is followed to its storage.
std::vector<std::size_t> BodyLowering::unsafe_reach(const Locals& locals) const {
    std::vector<bool> reached(locals.size(), false);
    for (std::size_t index = 0; index < locals.size(); ++index) {
        const Local& entry = locals[index];
        if (entry.binder.has_value()) {
            continue;
        }
        const bool reachable =
            entry.is_deref() || entry.external || escaped.contains(clang_hashCursor(entry.declaration));
        if (!reachable) {
            continue;
        }
        const std::size_t storage = entry.referent.value_or(index);
        if (storage < reached.size() && !locals[storage].binder.has_value()) {
            reached[storage] = true;
        }
    }
    // A place an unsafe block reaches gives it the address of the whole
    // object the place is part of, and pointer arithmetic from there is
    // valid C++ (TCB-UNSAFE-002): a view reached reaches the container it
    // views, and an element or member reached reaches every place of the
    // same object, the container itself included. This repeats until
    // nothing new is reached, since each step can lead to another.
    for (bool grew = true; grew;) {
        grew = false;
        const auto reach = [&](std::size_t target) {
            if (target < reached.size() && !reached[target] && !locals[target].binder.has_value()) {
                reached[target] = true;
                grew = true;
            }
        };
        for (std::size_t index = 0; index < locals.size(); ++index) {
            if (!reached[index]) {
                continue;
            }
            const Local& entry = locals[index];
            if (const std::optional<Local::Sequence>& held = entry.sequence;
                held.has_value() && held->views.has_value()) {
                reach(*held->views);
            }
            if (!entry.is_deref() && !entry.path.empty()) {
                for (std::size_t other = 0; other < locals.size(); ++other) {
                    if (!locals[other].referent.has_value() && !locals[other].is_deref() &&
                        clang_equalCursors(locals[other].declaration, entry.declaration) != 0) {
                        reach(other);
                    }
                }
            }
        }
    }
    std::vector<std::size_t> found;
    for (std::size_t index = 0; index < reached.size(); ++index) {
        if (reached[index] && !locals[index].referent.has_value()) {
            found.push_back(index);
        }
    }
    return found;
}

// An unsafe block on this path (SPEC.md 26, INTERACT-018, BOUNDARYEX-010).
//
// Its statements run as ordinary C++ and are not lowered: nothing they
// compute is known, and they establish no fact (UNSAFE-003, UNSAFE-005).
// Every place they could have written gets a version no earlier fact
// describes, which inherits nothing -- not even its declared refinement, since
// nothing charged the predicate at the block's writes (TRUST.md
// TCB-UNSAFE-003). From here on the path holds none of its contract's
// capabilities either. A block the path does not simply pass through is
// refused: one a return, a goto, or a break or continue of an enclosing loop
// leaves would make what follows depend on code nobody checked.
std::optional<Expr> BodyLowering::lower_unsafe(CXCursor block, CXCursor marker, const Continuation& next,
                                               const Locals& locals, unsigned depth) {
    const std::string name = take(clang_getCursorSpelling(marker));
    const source::SourceLocation where = presumed_location(clang_getCursorLocation(marker));
    const std::string at = where.file + ":" + std::to_string(where.line);
    if (const std::optional<std::string> left = leaves_block(block, 0, 0, 0)) {
        return reject("control leaves the unsafe block at " + at + " through " + *left +
                      "; a verified body passes through an unsafe block and goes on after it, so nothing in it "
                      "may return or jump out of it");
    }
    // Proof syntax inside the block is refused where it is recognized; a
    // generated declaration found here anyway is never read as a statement.
    const auto generated_inside = [&] {
        std::pair<std::string, bool> found{invariant_prefix, false};
        clang_visitChildren(
            block,
            [](CXCursor cursor, CXCursor, CXClientData data) {
                auto& search = *static_cast<std::pair<std::string, bool>*>(data);
                const std::string spelled = take(clang_getCursorSpelling(cursor));
                if (clang_getCursorKind(cursor) == CXCursor_VarDecl && spelled.starts_with(search.first) &&
                    !spelled.starts_with(search.first + "unsafe_")) {
                    search.second = true;
                    return CXChildVisit_Break;
                }
                return CXChildVisit_Recurse;
            },
            &found);
        return found.second;
    };
    if (generated_inside()) {
        return reject("the unsafe block at " + at + " holds proof syntax, which no path of the body reaches");
    }

    Locals state = locals;
    const std::vector<std::size_t> reached = unsafe_reach(state);
    // A refined element type is a content invariant of the container's
    // storage, and nothing obliges the block to leave only such values in
    // it (STDMODEL-020, TCB-UNSAFE-003).
    for (const std::size_t index : reached) {
        if (const std::optional<Local::Sequence>& held = state[index].sequence;
            held.has_value() && !held->element.refinements.empty()) {
            return reject("the unsafe block at " + at + " may write the elements of '" + state[index].spelling +
                          "', whose elements must satisfy '" + held->element.refinements.front().name +
                          "'; nothing obliges it to leave only such values there, so a container whose element "
                          "type is refined is not reached by an unsafe block");
        }
    }
    for (const std::size_t index : reached) {
        // The block may have replaced or ended a container's storage: every
        // view of it formed before is stale (STDMODEL-015).
        new_generation(state[index], "the unsafe block at " + at);
    }
    consumed_unsafe.push_back(name);
    const std::optional<source::SourceLocation> enclosing = revoked_by;
    if (!revoked_by.has_value()) {
        revoked_by = where;
    }
    std::optional<Expr> body = lower_statements(next, state, depth + 1);
    revoked_by = enclosing;
    if (!body) {
        return std::nullopt;
    }
    for (const std::size_t index : std::views::reverse(reached)) {
        *body = unknown(state, index, std::move(*body), block);
    }
    Expr region;
    region.type = body->type;
    region.location = where;
    region.node = UnsafeRegion{name, {std::move(*body)}};
    return region;
}

} // namespace cppl::clangbridge::detail
