#include "ghost.hpp"

#include "access.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "default_arguments.hpp"
#include "expressions.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "refinements.hpp"
#include "types.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Ghost state (SPEC.md 25, GHOST-001, GHOST-002): what a verified body declares
// as ghost state, every error in how it declares or uses it, and its lowering
// as a proof-only term binding. The check is erasure TCB (TRUST.md 21).
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::take;

namespace {

// Ghost state is an integer or a Boolean value. A class, pointer, reference or
// array could construct, destroy or alias runtime objects, and a volatile read
// is itself an effect (SPEC.md GHOST-001).
bool ghost_scalar(CXType type) {
    const CXType canonical = clang_getCanonicalType(type);
    if (clang_isVolatileQualifiedType(canonical) != 0) {
        return false;
    }
    switch (canonical.kind) {
        case CXType_Bool:
        case CXType_Char_U:
        case CXType_UChar:
        case CXType_UShort:
        case CXType_UInt:
        case CXType_ULong:
        case CXType_ULongLong:
        case CXType_Char_S:
        case CXType_SChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long:
        case CXType_LongLong:
            return true;
        default:
            return false;
    }
}

// The first effect a ghost initializer would have if it ran, if any. It never
// runs, so an effect it asks for would silently not happen (SPEC.md GHOST-001).
std::optional<std::string> ghost_effect(CXCursor cursor, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return "an expression nested too deeply to check";
    }
    const CXCursorKind kind = clang_getCursorKind(cursor);
    const std::vector<CXCursor> children = children_of(cursor);
    if ((kind == CXCursor_BinaryOperator && clang_getCursorBinaryOperatorKind(cursor) == CXBinaryOperator_Assign) ||
        kind == CXCursor_CompoundAssignOperator) {
        return "an assignment";
    }
    if (kind == CXCursor_UnaryOperator) {
        const enum CXUnaryOperatorKind op = clang_getCursorUnaryOperatorKind(cursor);
        if (op == CXUnaryOperator_PreInc || op == CXUnaryOperator_PostInc || op == CXUnaryOperator_PreDec ||
            op == CXUnaryOperator_PostDec) {
            return "an increment or decrement";
        }
        if (op == CXUnaryOperator_AddrOf) {
            return "an address taken";
        }
    }
    if (kind == CXCursor_CXXNewExpr || kind == CXCursor_CXXDeleteExpr) {
        return "an allocation";
    }
    if (kind == CXCursor_CXXThrowExpr) {
        return "a throw";
    }
    if (kind == CXCursor_LambdaExpr || kind == CXCursor_StmtExpr) {
        return "code of its own";
    }
    if (kind == CXCursor_DeclRefExpr &&
        clang_isVolatileQualifiedType(clang_getCursorType(clang_getCursorReferenced(cursor))) != 0) {
        return "a volatile read";
    }
    for (const CXCursor child : children) {
        if (std::optional<std::string> found = ghost_effect(child, depth + 1)) {
            return found;
        }
    }
    // A default argument a call relies on is evaluated by the call, so its
    // effects are the initializer's too (SPEC.md R.16).
    if (kind == CXCursor_CallExpr) {
        const CXCursor callee = clang_getCursorReferenced(cursor);
        const int count = clang_Cursor_getNumArguments(cursor);
        for (int index = 0; index < count; ++index) {
            const CXCursor argument = clang_Cursor_getArgument(cursor, static_cast<unsigned>(index));
            if (!is_default_argument(argument)) {
                continue;
            }
            const std::expected<CXCursor, std::string> initializer =
                default_argument_of(callee, static_cast<unsigned>(index));
            if (!initializer) {
                return "a default argument that is not resolved: " + initializer.error();
            }
            if (std::optional<std::string> found = ghost_effect(*initializer, depth + 1)) {
                return *found + " in the default argument of " + default_owner(callee, static_cast<unsigned>(index));
            }
        }
    }
    return std::nullopt;
}

} // namespace

GhostScan::GhostScan(std::string prefix) : prefix_(std::move(prefix)) {}

void GhostScan::run(CXCursor body) {
    declare(body, 0);
    if (!ghosts_.empty()) {
        leaks(body, false, 0);
    }
}

void GhostScan::error(std::string message, std::string note, CXCursor at) {
    errors.push_back(
        Function::GhostError{std::move(message), std::move(note), presumed_location(clang_getCursorLocation(at))});
}

bool GhostScan::is_ghost(CXCursor declaration) const {
    return std::ranges::any_of(ghosts_, [&](CXCursor ghost) { return clang_equalCursors(ghost, declaration) != 0; });
}

void GhostScan::declare(CXCursor cursor, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        error("statements nested too deeply to check for ghost state", {}, cursor);
        return;
    }
    const std::vector<CXCursor> children = children_of(cursor);
    if (clang_getCursorKind(cursor) == CXCursor_CompoundStmt) {
        for (std::size_t index = 0; index < children.size(); ++index) {
            if (!ghost_marker_of(children[index], prefix_)) {
                continue;
            }
            if (index + 1 >= children.size() || clang_getCursorKind(children[index + 1]) != CXCursor_DeclStmt) {
                error("this ghost declaration was not resolved", {}, children[index]);
                continue;
            }
            for (const CXCursor declared : children_of(children[index + 1])) {
                admit(declared);
            }
        }
    }
    for (const CXCursor child : children) {
        declare(child, depth + 1);
    }
}

void GhostScan::admit(CXCursor declaration) {
    const std::string name = take(clang_getCursorSpelling(declaration));
    if (clang_getCursorKind(declaration) != CXCursor_VarDecl) {
        error("a ghost declaration declares only variables", {}, declaration);
        return;
    }
    ghosts_.push_back(declaration);
    const enum CX_StorageClass storage = clang_Cursor_getStorageClass(declaration);
    if ((storage != CX_SC_None && storage != CX_SC_Auto) || clang_getCursorTLSKind(declaration) != CXTLS_None) {
        error("ghost '" + name + "' is not a local with automatic storage",
              "ghost state is local to one verified body (SPEC.md 25)", declaration);
        return;
    }
    const CXType type = clang_getCursorType(declaration);
    if (!ghost_scalar(type)) {
        error("ghost '" + name + "' has type '" + take(clang_getTypeSpelling(type)) +
                  "'; ghost state is an integer or a Boolean value",
              "a class, a pointer, a reference or an array could construct, destroy or alias runtime objects, "
              "and a volatile object is read by an effect (SPEC.md GHOST-001, GHOST-002)",
              declaration);
        return;
    }
    const CXCursor initializer = clang_Cursor_getVarDeclInitializer(declaration);
    if (clang_Cursor_isNull(initializer) != 0) {
        error("ghost '" + name + "' is declared without a value",
              "ghost state holds the value it is declared with, and nothing may write it later", declaration);
        return;
    }
    if (const std::optional<std::string> effect = ghost_effect(initializer, 0)) {
        error("the initializer of ghost '" + name + "' has " + *effect,
              "a ghost declaration leaves the program whole, so its initializer never runs and may have no effect "
              "(SPEC.md GHOST-001)",
              initializer);
        return;
    }
    collect_calls(initializer, name, 0);
}

void GhostScan::collect_calls(CXCursor cursor, const std::string& ghost, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return;
    }
    if (clang_getCursorKind(cursor) == CXCursor_CallExpr) {
        const CXCursor callee = clang_getCursorReferenced(cursor);
        calls.push_back(Function::GhostCall{
            clang_Cursor_isNull(callee) != 0 ? std::string{} : take(clang_getCursorUSR(callee)),
            take(clang_getCursorSpelling(cursor)), ghost, presumed_location(clang_getCursorLocation(cursor))});
        // What a default argument the call relies on calls, the initializer
        // calls too (SPEC.md R.16). One that is not resolved has been
        // refused by `ghost_effect` already.
        const int count = clang_Cursor_getNumArguments(cursor);
        for (int index = 0; index < count; ++index) {
            const CXCursor argument = clang_Cursor_getArgument(cursor, static_cast<unsigned>(index));
            if (!is_default_argument(argument)) {
                continue;
            }
            if (const std::expected<CXCursor, std::string> initializer =
                    default_argument_of(callee, static_cast<unsigned>(index))) {
                collect_calls(*initializer, ghost, depth + 1);
            }
        }
    }
    for (const CXCursor child : children_of(cursor)) {
        collect_calls(child, ghost, depth + 1);
    }
}

void GhostScan::leaks(CXCursor cursor, bool proof_only, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        error("statements nested too deeply to check for uses of ghost state", {}, cursor);
        return;
    }
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_VarDecl) {
        proof_only = proof_only || take(clang_getCursorSpelling(cursor)).starts_with(prefix_) || is_ghost(cursor);
    }
    if (kind == CXCursor_DeclRefExpr && !proof_only) {
        const CXCursor referenced = clang_getCursorReferenced(cursor);
        if (is_ghost(referenced)) {
            error("ghost '" + take(clang_getCursorSpelling(referenced)) + "' is used by code that runs",
                  "ghost state leaves the program before it runs, so no returned value, branch, index, argument, "
                  "initializer or write may depend on it (SPEC.md GHOST-002)",
                  cursor);
        }
    }
    for (const CXCursor child : children_of(cursor)) {
        leaks(child, proof_only, depth + 1);
    }
}

// Whether `statement` is the declaration the projector put just before a ghost
// declaration (SPEC.md 25), which is then the next statement of the block.
bool ghost_marker_of(CXCursor statement, const std::string& prefix) {
    if (prefix.empty() || clang_getCursorKind(statement) != CXCursor_DeclStmt) {
        return false;
    }
    const std::vector<CXCursor> declared = children_of(statement);
    return declared.size() == 1 && clang_getCursorKind(declared.front()) == CXCursor_VarDecl &&
           take(clang_getCursorSpelling(declared.front())).starts_with(prefix + "ghost_");
}

// Ghost state declared here (SPEC.md 25): each variable the declaration
// after the marker declares is a proof-only value, stated as a term over the
// versions current here. Its initializer never runs, so it is read the way a
// specification expression is, and nothing it names is written. Whether the
// declaration and every use of it are admissible was decided before the body
// was lowered (`scan_ghost_state`).
std::optional<Expr> BodyLowering::lower_ghost(const Continuation& from, const Locals& locals, unsigned depth) {
    const std::vector<CXCursor>& statements = *from.statements;
    if (from.index + 1 >= statements.size() || clang_getCursorKind(statements[from.index + 1]) != CXCursor_DeclStmt) {
        return reject("a ghost declaration was not resolved");
    }
    // No label of a switch follows a ghost declaration in the same body: a
    // jump to it would bypass the declaration's initialization.
    return lower_ghost_declaration(children_of(statements[from.index + 1]), 0,
                                   Continuation{from.outer, from.statements, from.index + 2}, locals, depth);
}

std::optional<Expr> BodyLowering::lower_ghost_declaration(const std::vector<CXCursor>& declared, std::size_t index,
                                                          const Continuation& next, const Locals& locals,
                                                          unsigned depth) {
    if (index == declared.size()) {
        return lower_statements(next, locals, depth + 1);
    }
    const CXCursor declaration = declared[index];
    const std::string name = take(clang_getCursorSpelling(declaration));
    CXCursor initializer = clang_getCursorKind(declaration) == CXCursor_VarDecl
                               ? clang_Cursor_getVarDeclInitializer(declaration)
                               : clang_getNullCursor();
    if (clang_Cursor_isNull(initializer) != 0) {
        return reject("ghost '" + name + "' was not resolved");
    }
    const CXType written = clang_getCursorType(declaration);
    Type type = convert_type(written, 0, ReferenceModel::Opaque, refinements);
    if (type.kind != TypeKind::Int && type.kind != TypeKind::Bool) {
        return reject("ghost '" + name + "' has type '" + type.spelling + "', which is not modeled");
    }
    if (refinements != nullptr) {
        auto resolved = refinements_of(declaration, written, *refinements);
        if (!resolved)
            return reject(resolved.error().message);
        type.refinements = std::move(*resolved);
    }
    if (clang_getCursorKind(initializer) == CXCursor_InitListExpr) {
        const std::vector<CXCursor> elements = children_of(initializer);
        if (elements.size() != 1) {
            return reject("the initializer of ghost '" + name + "' is not a single modeled value");
        }
        initializer = elements[0];
    }
    Expr value = build_expression(initializer, signature, locals, 0);
    if (!std::holds_alternative<Unsupported>(value.node) && !same_modeled_value(type, value.type)) {
        return reject("initializing ghost '" + name + "' of type '" + type.spelling + "' from '" + value.type.spelling +
                      "' is a conversion that is not modeled");
    }
    const std::uint32_t version = next_version++;
    Locals declaring = locals;
    declaring.push_back(Local{.declaration = declaration, .version = version, .type = type, .spelling = name});
    std::optional<Expr> body = lower_ghost_declaration(declared, index + 1, next, declaring, depth);
    if (!body) {
        return std::nullopt;
    }
    return bind(version, place_of(declaring, declaring.size() - 1), std::move(value), std::move(*body), declaration,
                type);
}

} // namespace cppl::clangbridge::detail
