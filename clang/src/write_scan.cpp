#include "write_scan.hpp"

#include "access.hpp"
#include "call_objects.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
#include "places.hpp"
#include "sequences.hpp"
#include "signature.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <optional>
#include <unordered_set>
#include <vector>

// Which storage a body or a loop writes, and which locals a pointer or an
// unmodeled write may reach (RFC 0014 §4, §6): the scans the lowering runs over
// a body before it lowers a statement of it.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::strip_parens;

namespace {

// Whether two paths into one object may name overlapping storage: one runs
// through the other, or they part only at a step that decides nothing. A
// symbolic element may be any element, so it overlaps every sibling element.
bool paths_overlap(const std::vector<PlaceStep>& lhs, const std::vector<PlaceStep>& rhs) {
    const std::size_t common = std::min(lhs.size(), rhs.size());
    for (std::size_t step = 0; step < common; ++step) {
        if (lhs[step].kind == PlaceStep::Kind::SymbolicElement || rhs[step].kind == PlaceStep::Kind::SymbolicElement) {
            continue;
        }
        if (lhs[step].kind != rhs[step].kind || lhs[step].index != rhs[step].index) {
            return false;
        }
    }
    return true;
}

// Marks every entry a write to the storage `access` names may land on: the
// entries of its object whose path runs through the storage written or into
// it. A sibling member is distinct storage and is left alone.
void mark_access(const ResolvedAccess& access, const WriteScan& scan) {
    for (std::size_t index = 0; index < scan.locals->size(); ++index) {
        const Local& entry = (*scan.locals)[index];
        if (clang_equalCursors(entry.declaration, access.declaration) != 0 && paths_overlap(entry.path, access.path)) {
            (*scan.written)[index] = true;
        }
    }
}

void mark_write(CXCursor cursor, const WriteScan& scan) {
    const CXCursorKind kind = clang_getCursorKind(cursor);
    const bool assigns =
        (kind == CXCursor_BinaryOperator && clang_getCursorBinaryOperatorKind(cursor) == CXBinaryOperator_Assign) ||
        kind == CXCursor_CompoundAssignOperator;
    bool updates = false;
    if (kind == CXCursor_UnaryOperator) {
        const enum CXUnaryOperatorKind op = clang_getCursorUnaryOperatorKind(cursor);
        updates = op == CXUnaryOperator_PreInc || op == CXUnaryOperator_PostInc || op == CXUnaryOperator_PreDec ||
                  op == CXUnaryOperator_PostDec;
    }
    if (kind == CXCursor_CallExpr) {
        const auto callee = clang_getCursorReferenced(cursor);
        const auto parameters = parameters_of(callee);
        bool writes = std::ranges::any_of(parameters, [](CXCursor parameter) {
            return source::may_write(passing_of(clang_getCursorType(parameter)));
        });
        // A member function may write its object through `this`, and a call
        // that writes anything may write the object through an alias
        // (SPEC.md CLASS-011).
        // `a = b` written between two objects of one class writes `a` whole,
        // whichever assignment operator Clang selected (TRUST.md
        // TCB-AGGREGATE-001).
        if (clang_getCursorKind(callee) == CXCursor_CXXMethod && clang_Cursor_getNumArguments(cursor) == 2 &&
            (clang_CXXMethod_isCopyAssignmentOperator(callee) != 0 ||
             clang_CXXMethod_isMoveAssignmentOperator(callee) != 0)) {
            if (const auto access = resolve_access(designated_object(clang_Cursor_getArgument(cursor, 0)));
                access && !access->dereferenced) {
                mark_access(*access, scan);
            }
        }
        if (clang_getCursorKind(callee) == CXCursor_CXXMethod && clang_CXXMethod_isStatic(callee) == 0) {
            const auto receiver = receiver_of(callee, nullptr);
            const auto object = call_object(cursor, callee);
            if (receiver && object && (writes || receiver->writes())) {
                writes = true;
                mark_access(ResolvedAccess{cursor, object->path, false, {}, object->receiver, object->declaration},
                            scan);
            }
        }
        if (writes) {
            for (std::size_t index = 0; index < parameters.size(); ++index) {
                if (!source::aliases_storage(passing_of(clang_getCursorType(parameters[index]))))
                    continue;
                auto argument = clang_Cursor_getArgument(cursor, static_cast<unsigned>(index));
                while (clang_getCursorKind(argument) == CXCursor_UnexposedExpr ||
                       clang_getCursorKind(argument) == CXCursor_ParenExpr) {
                    const auto inner = children_of(argument);
                    if (inner.size() != 1)
                        break;
                    argument = inner.front();
                }
                if (auto storage = written_storage(clang_getCursorReferenced(argument), *scan.locals)) {
                    (*scan.written)[*storage] = true;
                } else if (const auto access = resolve_access(argument); access && !access->dereferenced) {
                    mark_access(*access, scan);
                }
            }
        }
    }
    if (!assigns && !updates) {
        return;
    }
    const std::vector<CXCursor> operands = children_of(cursor);
    if (operands.empty()) {
        return;
    }
    CXCursor target = operands[0];
    while (clang_getCursorKind(target) == CXCursor_ParenExpr) {
        const std::vector<CXCursor> inner = children_of(target);
        if (inner.size() != 1) {
            return;
        }
        target = inner[0];
    }
    if (clang_getCursorKind(target) != CXCursor_DeclRefExpr) {
        // An element of a vector, a string or a span is storage of the
        // container it belongs to or views, not of the object named here; what
        // a write to one reaches is decided with the alias analysis, in
        // `mark_sequence_writes` (RFC 0020 §3).
        if (const std::optional<SequenceCall> element = sequence_call(target);
            element && !element->constructor && element->name == "operator[]" && source::is_sequence(element->family)) {
            return;
        }
        // Writing a member or an element writes the object it belongs to, so
        // the entries tracking that storage are the ones this reaches. Which of
        // them the write lands on is decided when the statement is lowered;
        // here it is only a question of which entries must be kept, and keeping
        // one that turns out untouched costs nothing.
        if (const auto access = resolve_access(target)) {
            mark_access(*access, scan);
        }
        return;
    }
    if (const auto local = written_storage(clang_getCursorReferenced(target), *scan.locals)) {
        (*scan.written)[*local] = true;
    }
}

} // namespace

// The locals whose address this body takes, by Clang's resolution of `&x` and
// of an array decaying to a pointer.
//
// A local absent from this set cannot be the pointee of any pointer in the
// body, so a write through a pointer cannot reach it. That is the only
// precision claimed here: everything address-taken stays permanently at risk,
// because a pointer formed on one path may be written through on another
// (RFC 0014 §4, §6).
std::unordered_set<unsigned> escaped_locals(CXCursor body) {
    std::unordered_set<unsigned> escaped;
    clang_visitChildren(
        body,
        [](CXCursor cursor, CXCursor, CXClientData data) {
            auto& found = *static_cast<std::unordered_set<unsigned>*>(data);
            const auto record = [&](CXCursor operand) {
                operand = strip_parens(operand);
                // A member or element of an object puts the whole object at
                // risk: the pointer reaches storage inside it.
                if (const auto access = resolve_access(operand)) {
                    const auto declaration = access->declaration;
                    if (clang_getCursorKind(declaration) == CXCursor_VarDecl ||
                        clang_getCursorKind(declaration) == CXCursor_ParmDecl) {
                        found.insert(clang_hashCursor(declaration));
                    }
                }
            };
            if (clang_getCursorKind(cursor) == CXCursor_UnaryOperator &&
                clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_AddrOf) {
                const auto children = children_of(cursor);
                if (children.size() == 1) {
                    record(children[0]);
                }
            }
            // An array used as a value decays to a pointer to its first
            // element, which escapes it just as `&a[0]` would.
            if (clang_getCursorKind(cursor) == CXCursor_DeclRefExpr &&
                clang_getCanonicalType(clang_getCursorType(cursor)).kind == CXType_ConstantArray) {
                record(cursor);
            }
            return CXChildVisit_Recurse;
        },
        &escaped);
    return escaped;
}

// The locals some write outside this body's model could reach.
//
// This asks a stricter question than `escaped_locals` and the two must not be
// confused. `escaped_locals` answers "could a pointer in this body point here",
// and to that end it counts every array-to-pointer decay -- including the one
// every subscript performs on its own base. That is the right answer for
// aliasing and a useless one here, because it marks every array that is ever
// indexed.
//
// The decay a subscript performs on its own base is not an escape: the pointer
// selects one element, does not outlive the expression, and the access it
// serves goes through the place machinery, which charges the element type's
// refinement on every write. Every other appearance of an array is an escape --
// a decay as a call argument, a decay into pointer arithmetic, binding the
// array to a reference -- as is taking the address of the object or of anything
// inside it (RFC 0014 §4, §6).
std::unordered_set<unsigned> unconfined_locals(CXCursor body) {
    std::unordered_set<unsigned> escaped;
    const auto record = [&escaped](CXCursor operand) {
        operand = strip_parens(operand);
        if (const auto access = resolve_access(operand)) {
            const auto declaration = access->declaration;
            if (clang_getCursorKind(declaration) == CXCursor_VarDecl ||
                clang_getCursorKind(declaration) == CXCursor_ParmDecl) {
                escaped.insert(clang_hashCursor(declaration));
            }
        }
    };
    // Recursive rather than `clang_visitChildren`: whether a decay escapes
    // depends on what consumes it, and only the parent knows that.
    const auto walk = [&record](auto&& self, CXCursor cursor) -> void {
        const auto kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_ArraySubscriptExpr) {
            const auto children = children_of(cursor);
            if (children.size() == 2) {
                // The base's own decay is consumed here. Anything further
                // inside it is not, so a base that is not a plain array name
                // is walked as usual.
                const auto base = strip_parens(children[0]);
                if (clang_getCursorKind(base) != CXCursor_DeclRefExpr ||
                    clang_getCanonicalType(clang_getCursorType(base)).kind != CXType_ConstantArray) {
                    self(self, children[0]);
                }
                self(self, children[1]);
                return;
            }
        }
        if (kind == CXCursor_UnaryOperator && clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_AddrOf) {
            const auto children = children_of(cursor);
            if (children.size() == 1) {
                record(children[0]);
            }
        }
        if (kind == CXCursor_DeclRefExpr &&
            clang_getCanonicalType(clang_getCursorType(cursor)).kind == CXType_ConstantArray) {
            record(cursor);
        }
        for (const auto child : children_of(cursor)) {
            self(self, child);
        }
    };
    walk(walk, body);
    return escaped;
}

std::optional<std::size_t> written_storage(CXCursor declaration, const Locals& locals, unsigned depth) {
    if (const auto local = find_local(locals, declaration))
        return local;
    if (depth > kMaxExpressionDepth || clang_getCursorKind(declaration) != CXCursor_VarDecl)
        return std::nullopt;
    const auto type = clang_getCanonicalType(clang_getCursorType(declaration));
    if (type.kind != CXType_LValueReference && type.kind != CXType_RValueReference)
        return std::nullopt;
    auto initializer = clang_Cursor_getVarDeclInitializer(declaration);
    while (clang_getCursorKind(initializer) == CXCursor_UnexposedExpr ||
           clang_getCursorKind(initializer) == CXCursor_ParenExpr) {
        const auto inner = children_of(initializer);
        if (inner.size() != 1)
            return std::nullopt;
        initializer = inner[0];
    }
    return clang_getCursorKind(initializer) == CXCursor_DeclRefExpr
               ? written_storage(clang_getCursorReferenced(initializer), locals, depth + 1)
               : std::nullopt;
}

void mark_writes(CXCursor root, const Locals& locals, std::vector<bool>& written) {
    WriteScan scan{&locals, &written};
    mark_write(root, scan);
    clang_visitChildren(
        root,
        [](CXCursor child, CXCursor, CXClientData data) {
            mark_write(child, *static_cast<const WriteScan*>(data));
            return CXChildVisit_Recurse;
        },
        &scan);
    // A written external place may be any other external place: storage the
    // caller supplies is kept apart only where Clang resolves it apart, which
    // the lowering decides (`BodyLowering::may_alias`). Following one more
    // place than a write reaches costs nothing but a version.
    for (std::size_t target = 0; target < locals.size(); ++target) {
        if (!written[target] || !locals[target].external)
            continue;
        for (std::size_t other = 0; other < locals.size(); ++other)
            if (locals[other].external && !locals[other].referent)
                written[other] = true;
    }
}

} // namespace cppl::clangbridge::detail
