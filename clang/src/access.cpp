#include "access.hpp"

#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
#include "expressions.hpp"
#include "places.hpp"
#include "sequences.hpp"
#include "signature.hpp"
#include "types.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// How an access names storage (SPEC.md 12.10, RFC 0014): the place a tracked
// entry denotes, how a parameter's type passes storage, the one resolver every
// access form walks, the lookup of a tracked place and the one read.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::record_fields;
using bridge::strip_parens;

namespace {

// A field's position among its record's data members, counted the same way the
// representation's components are, so a component index and a field index
// denote the same member. A bit-field has no modeled value, so it has no place.
std::optional<std::uint32_t> field_index_of(CXCursor field) {
    if (clang_getFieldDeclBitWidth(field) >= 0)
        return std::nullopt;
    const CXCursor record = clang_getCursorSemanticParent(field);
    if (clang_getCursorKind(record) == CXCursor_UnionDecl)
        return std::nullopt;
    // Counted over the record's type, by the same walk that builds the
    // components, so a component index and a field index stay the same number
    // for an instantiated class template as for an ordinary record.
    const auto fields = record_fields(clang_getCursorType(record));
    for (std::size_t index = 0; index < fields.size(); ++index)
        if (clang_equalCursors(fields[index], field) != 0)
            return static_cast<std::uint32_t>(index);
    return std::nullopt;
}

bool same_terms(const std::vector<Expr>& lhs, const std::vector<Expr>& rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin(), same_term);
}

} // namespace

// The place a tracked entry denotes, as the VIR node carries it.
//
// The root identifies the object, so every place projected out of one object
// shares its root and the path distinguishes the members. The id is the index
// of the entry that roots the object: the first entry declaring it, which is
// stable for the lowering of one body. A reference resolves to its referent
// first, because a write through it is a write to that storage (SPEC.md 12.9).
Place place_of(const Locals& locals, std::size_t entry) {
    const std::size_t storage = locals[entry].referent.value_or(entry);
    // A dereference is rooted in the pointer it dereferences, not in a
    // declaration, and is distinguished by the pointer version it read.
    if (const std::optional<std::size_t>& pointer = locals[storage].pointer; pointer.has_value()) {
        Place place;
        place.root.kind = PlaceRoot::Kind::Deref;
        place.root.id = static_cast<std::uint32_t>(*pointer);
        place.root.version = locals[storage].pointer_version;
        place.path = locals[storage].path;
        place.spelling = locals[entry].spelling;
        return place;
    }
    std::size_t root = storage;
    for (std::size_t index = 0; index < locals.size(); ++index) {
        if (clang_equalCursors(locals[index].declaration, locals[storage].declaration) != 0 &&
            !locals[index].referent.has_value()) {
            root = index;
            break;
        }
    }
    Place place;
    place.root.kind = locals[storage].external ? PlaceRoot::Kind::Parameter : PlaceRoot::Kind::Local;
    place.root.id = static_cast<std::uint32_t>(root);
    place.path = locals[storage].path;
    place.spelling = locals[entry].spelling;
    return place;
}

// Preserve pointee sugar while following aliases to a reference. Canonicalizing
// first would discard the refinement attached to that pointee.
CXType reference_value_type(CXType written) {
    for (unsigned depth = 0; depth < kMaxExpressionDepth; ++depth) {
        if (written.kind == CXType_LValueReference || written.kind == CXType_RValueReference)
            return clang_getPointeeType(written);
        const auto declaration = clang_getTypeDeclaration(written);
        const auto underlying = clang_getTypedefDeclUnderlyingType(declaration);
        if (underlying.kind == CXType_Invalid)
            break;
        written = underlying;
    }
    return CXType{CXType_Invalid, {nullptr, nullptr}};
}

source::ParameterPassing passing_of(CXType written) {
    const auto canonical = clang_getCanonicalType(written);
    if (canonical.kind != CXType_LValueReference && canonical.kind != CXType_RValueReference)
        return source::ParameterPassing::Value;
    if (clang_isConstQualifiedType(clang_getPointeeType(canonical)))
        return source::ParameterPassing::ConstReference;
    return canonical.kind == CXType_RValueReference ? source::ParameterPassing::RvalueReference
                                                    : source::ParameterPassing::MutableReference;
}

// Whether passing this parameter lets the callee write storage the caller can
// still name afterwards. A pointer is passed by value, so `passing_of` calls it
// `Value` and the parameter's own version is unaffected -- but the callee may
// write through it, and the caller's facts about the pointee do not survive
// that (SPEC.md 12.10 VERIFIED-040, VERIFIED-041). A pointer to const is
// excluded: writing through it is not something the callee may do.
bool may_write_through(CXType written) {
    if (source::may_write(passing_of(written))) {
        return true;
    }
    const auto canonical = clang_getCanonicalType(written);
    if (canonical.kind == CXType_Pointer) {
        return clang_isConstQualifiedType(clang_getPointeeType(canonical)) == 0U;
    }
    // A span of non-const elements designates storage the callee may write,
    // as a pointer to non-const does, and nothing keeps that storage apart
    // from what the caller reads by another name (STDMODEL-017).
    return convert_type(canonical).representation.kind == source::RepresentationKind::Span &&
           clang_isConstQualifiedType(clang_Type_getTemplateArgumentAsType(canonical, 0)) == 0U;
}

// Whether a parameter passed by value designates caller storage, whatever the
// constness of what it designates: a pointer, or a span.
bool designates_storage(CXType written) {
    const auto canonical = clang_getCanonicalType(written);
    return canonical.kind == CXType_Pointer ||
           convert_type(canonical).representation.kind == source::RepresentationKind::Span;
}

// Whether an element place of a sequence is still the place its subscript
// names: formed at the generation of its sequence that is current (RFC 0020
// §4). One formed earlier is never matched again, so an access after the
// storage may have changed forms a place of its own and owes its bound anew,
// against the length current there.
bool generation_current(const Locals& locals, const Local& entry) {
    return !entry.formed_at.has_value() ||
           (entry.formed_at->root < locals.size() && locals[entry.formed_at->root].version == entry.formed_at->version);
}

std::optional<std::size_t> find_binding(const Locals& locals, CXCursor declaration, const std::vector<PlaceStep>& path,
                                        const Expr* index_term) {
    for (std::size_t index = locals.size(); index > 0; --index) {
        if (locals[index - 1].same_place(declaration, path, index_term) &&
            generation_current(locals, locals[index - 1])) {
            return index - 1;
        }
    }
    return std::nullopt;
}

// Why using the view or element reference held in `binding` would use storage
// that may no longer exist, if it would (SPEC.md STDMODEL-015, RFC 0020 §4).
//
// Such a name was formed over a sequence's storage at one generation. Any
// operation that may reallocate, shrink, replace or end that storage gives the
// sequence a new one, and the storage the name designates may be gone: using
// it is undefined behavior, so it is refused rather than read as an unknown.
std::optional<std::string> stale_borrow(const Locals& locals, std::size_t binding) {
    const Local& entry = locals[binding];
    if (!entry.borrows.has_value()) {
        return std::nullopt;
    }
    if (entry.borrows->root >= locals.size()) {
        return "'" + entry.spelling + "' designates storage this body no longer tracks";
    }
    const Local& root = locals[entry.borrows->root];
    if (root.version == entry.borrows->version) {
        return std::nullopt;
    }
    const std::string what = entry.referent.has_value() ? "refers to an element of" : "views the storage of";
    const std::string since = root.sequence.has_value() && !root.sequence->invalidated.empty()
                                  ? root.sequence->invalidated
                                  : "an operation that may have replaced it";
    return "'" + entry.spelling + "' " + what + " '" + root.spelling +
           "', which may have been reallocated or ended by " + since +
           "; a view or element reference is used only while the storage it was formed over is unchanged "
           "(SPEC.md STDMODEL-015)";
}

std::optional<std::size_t> find_local(const Locals& locals, CXCursor declaration, const std::vector<PlaceStep>& path,
                                      const Expr* index_term) {
    const auto binding = find_binding(locals, declaration, path, index_term);
    return binding ? std::optional{locals[*binding].referent.value_or(*binding)} : std::nullopt;
}

// The canonical declaration of the class `member` is declared in: the identity
// the implicit object of a member function, and a member named without an
// object, resolve to.
CXCursor enclosing_record(CXCursor member) {
    return clang_getCanonicalCursor(clang_getCursorSemanticParent(member));
}

// The constant element index a subscript selects, when Clang evaluated one.
//
// A variable index selects no single element, so it becomes a symbolic element
// step instead (RFC 0014 §17 step 7). The two are different place kinds
// because a constant index is decided and a symbolic one is not: two symbolic
// elements are disjoint only when their indices are proved unequal.
std::optional<std::uint32_t> constant_index_of(CXCursor subscript) {
    CXEvalResult evaluated = clang_Cursor_Evaluate(subscript);
    if (evaluated == nullptr)
        return std::nullopt;
    const bool integral = clang_EvalResult_getKind(evaluated) == CXEval_Int;
    const long long index = integral ? clang_EvalResult_getAsLongLong(evaluated) : -1;
    clang_EvalResult_dispose(evaluated);
    if (index < 0 || index > std::numeric_limits<std::uint32_t>::max())
        return std::nullopt;
    return static_cast<std::uint32_t>(index);
}

// The class `this` designates an object of, where `cursor` is `this` itself.
std::optional<CXCursor> this_record(CXCursor cursor) {
    if (clang_getCursorKind(strip_parens(cursor)) != CXCursor_CXXThisExpr) {
        return std::nullopt;
    }
    const CXType pointee = clang_getPointeeType(clang_getCanonicalType(clang_getCursorType(strip_parens(cursor))));
    return clang_getCanonicalCursor(clang_getTypeDeclaration(pointee));
}

std::optional<ResolvedAccess> resolve_access(CXCursor cursor) {
    std::vector<PlaceStep> path;
    std::vector<CXCursor> symbolic;
    bool dereferenced = false;
    // The data member the implicit object was reached through last, whose class
    // is the object's: a member of a base class names the base, never the
    // derived object the conversion started from.
    CXCursor outermost_field = clang_getNullCursor();
    // The path is built outermost-first and reversed at the end, so the
    // symbolic indices are reversed with it to stay in step order.
    const auto finish = [&](CXCursor object, bool through_pointer) {
        std::ranges::reverse(path);
        std::ranges::reverse(symbolic);
        return ResolvedAccess{
            object, std::move(path), through_pointer, std::move(symbolic), false, clang_getCursorReferenced(object)};
    };
    // An access rooted in the implicit object. Its class is the one the member
    // written next to it belongs to, or, for `*this` alone, the one `this`
    // points to.
    const auto finish_receiver = [&](CXCursor at, std::optional<CXCursor> record) {
        std::ranges::reverse(path);
        std::ranges::reverse(symbolic);
        const CXCursor root = clang_Cursor_isNull(outermost_field) == 0 ? enclosing_record(outermost_field)
                                                                        : record.value_or(clang_getNullCursor());
        if (clang_Cursor_isNull(root) != 0) {
            return std::optional<ResolvedAccess>{};
        }
        return std::optional<ResolvedAccess>{
            ResolvedAccess{at, std::move(path), false, std::move(symbolic), true, root}};
    };
    cursor = strip_parens(cursor);
    for (unsigned depth = 0; depth < kMaxExpressionDepth; ++depth) {
        const auto kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_DeclRefExpr) {
            return finish(cursor, dereferenced);
        }
        // A dereference roots the access in the pointee. Nothing may stand
        // between it and the declaration holding the pointer: a pointer
        // computed by arithmetic or returned by a call names storage this
        // implementation cannot identify, so it is refused rather than guessed.
        if (kind == CXCursor_UnaryOperator && clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_Deref) {
            const auto children = children_of(cursor);
            if (children.size() != 1 || dereferenced) {
                return std::nullopt;
            }
            // `*this` is the implicit object itself.
            if (const std::optional<CXCursor> record = this_record(children[0])) {
                return finish_receiver(cursor, record);
            }
            const auto pointer = strip_parens(children[0]);
            if (clang_getCursorKind(pointer) != CXCursor_DeclRefExpr) {
                return std::nullopt;
            }
            return finish(pointer, true);
        }
        // A subscript of `std::array` or of a modeled sequence is a call to its
        // `operator[]`, and selects an element exactly as a built-in subscript
        // does, so it takes the same step (RFC 0020 §3). What storage the step
        // lands in -- the array's own, a container's, the one a span views --
        // is the caller's to decide from the object's root.
        if (kind == CXCursor_CallExpr) {
            const std::optional<SequenceCall> call = sequence_call(cursor);
            if (!call || call->constructor || call->name != "operator[]" || call->arguments.size() != 1) {
                return std::nullopt;
            }
            if (const auto index = constant_index_of(call->arguments.front())) {
                path.push_back(PlaceStep{PlaceStep::Kind::Element, *index, 0});
            } else {
                path.push_back(
                    PlaceStep{PlaceStep::Kind::SymbolicElement, 0, static_cast<std::uint32_t>(symbolic.size())});
                symbolic.push_back(call->arguments.front());
            }
            cursor = strip_parens(call->object);
            continue;
        }
        const auto children = children_of(cursor);
        if (kind == CXCursor_MemberRefExpr) {
            const auto field = clang_getCursorReferenced(cursor);
            // A member named without an object, `x` in a member function, is
            // `this->x`: Clang leaves the implicit `this` out of the cursor
            // tree, so the member access has no child at all.
            const bool implicit_object = children.empty();
            const std::optional<CXCursor> explicit_object =
                children.size() == 1 ? this_record(children[0]) : std::nullopt;
            if (implicit_object || explicit_object.has_value()) {
                if (clang_getCursorKind(field) != CXCursor_FieldDecl || dereferenced) {
                    return std::nullopt;
                }
                const auto index = field_index_of(field);
                if (!index) {
                    return std::nullopt;
                }
                path.push_back(PlaceStep{PlaceStep::Kind::Field, *index});
                outermost_field = field;
                return finish_receiver(cursor, explicit_object);
            }
        }
        // `p->m` and `p[i]` dereference without a `*`: Clang leaves the operand
        // a pointer rather than inserting a visible dereference. Both are the
        // same access as `(*p).m` and `*(p + i)`, so they resolve to a deref
        // place and owe the same capability.
        if ((kind == CXCursor_MemberRefExpr || kind == CXCursor_ArraySubscriptExpr) && !children.empty() &&
            clang_getCanonicalType(clang_getCursorType(strip_parens(children[0]))).kind == CXType_Pointer) {
            if (dereferenced) {
                return std::nullopt;
            }
            dereferenced = true;
        }
        if (kind == CXCursor_MemberRefExpr) {
            const auto field = clang_getCursorReferenced(cursor);
            if (clang_getCursorKind(field) != CXCursor_FieldDecl || children.size() != 1)
                return std::nullopt;
            const auto index = field_index_of(field);
            if (!index)
                return std::nullopt;
            path.push_back(PlaceStep{PlaceStep::Kind::Field, *index});
            outermost_field = field;
        } else if (kind == CXCursor_ArraySubscriptExpr) {
            if (children.size() != 2)
                return std::nullopt;
            if (const auto index = constant_index_of(children[1])) {
                path.push_back(PlaceStep{PlaceStep::Kind::Element, *index, 0});
            } else {
                // A symbolic index selects an element this implementation
                // cannot decide. It is still one place -- the step records
                // which index term selects it -- and it is disjoint from
                // another element only where that is proved (RFC 0014 §4,
                // §17 step 7).
                path.push_back(
                    PlaceStep{PlaceStep::Kind::SymbolicElement, 0, static_cast<std::uint32_t>(symbolic.size())});
                symbolic.push_back(children[1]);
            }
        } else {
            return std::nullopt;
        }
        cursor = strip_parens(children[0]);
    }
    return std::nullopt;
}

// Whether two index terms are one term read at one set of versions.
//
// A symbolic element place is the storage its index selects, so two symbolic
// subscripts name the same place only when their indices are the same value.
// Deciding that is what keeps `a[i]` and `a[j]` apart: sharing one place would
// make a write at one index a fact about the other, which is false wherever the
// indices differ (RFC 0014 §4).
//
// The comparison is structural and errs toward difference: a shape not decided
// here is reported as a different term, which gives the access its own place and
// leaves `may_alias` to invalidate it. That costs precision and never soundness.
// Versions are compared and source locations are not, because one term read
// twice is written twice but denotes one value only while nothing it reads has
// been written.
bool same_term(const Expr& lhs, const Expr& rhs) {
    if (lhs.type != rhs.type || lhs.node.index() != rhs.node.index()) {
        return false;
    }
    if (const auto* literal = std::get_if<IntLiteral>(&lhs.node)) {
        return literal->value == std::get<IntLiteral>(rhs.node).value;
    }
    if (const auto* parameter = std::get_if<ParameterRef>(&lhs.node)) {
        return parameter->index == std::get<ParameterRef>(rhs.node).index;
    }
    if (const auto* read = std::get_if<PlaceRef>(&lhs.node)) {
        const auto& other = std::get<PlaceRef>(rhs.node);
        return read->version == other.version && read->place == other.place;
    }
    if (const auto* binary = std::get_if<Binary>(&lhs.node)) {
        const auto& other = std::get<Binary>(rhs.node);
        return binary->op == other.op && same_terms(binary->operands, other.operands);
    }
    if (const auto* negation = std::get_if<Negation>(&lhs.node)) {
        return same_terms(negation->operands, std::get<Negation>(rhs.node).operands);
    }
    if (const auto* minus = std::get_if<Minus>(&lhs.node)) {
        return same_terms(minus->operands, std::get<Minus>(rhs.node).operands);
    }
    // The types are compared above, so one conversion of one term is one value.
    if (const auto* conversion = std::get_if<Conversion>(&lhs.node)) {
        return same_terms(conversion->operands, std::get<Conversion>(rhs.node).operands);
    }
    if (const auto* projection = std::get_if<Projection>(&lhs.node)) {
        const auto& other = std::get<Projection>(rhs.node);
        return projection->index == other.index && same_terms(projection->operands, other.operands);
    }
    if (const auto* element = std::get_if<Element>(&lhs.node)) {
        return same_terms(element->operands, std::get<Element>(rhs.node).operands);
    }
    if (const auto* conditional = std::get_if<Conditional>(&lhs.node)) {
        return same_terms(conditional->operands, std::get<Conditional>(rhs.node).operands);
    }
    // Two calls spelled alike are two evaluations, and nothing available here
    // says they produce one value.
    return false;
}

// The entry holding a tracked dereference of `pointer` at `version`, with the
// given projection path, if this body already tracks it.
//
// A symbolic path is matched on its index term as well: the path alone records
// only that a step was symbolic, so every symbolic subscript of one pointee
// would otherwise be one place.
std::optional<std::size_t> find_deref(const Locals& locals, std::size_t pointer, std::uint32_t version,
                                      const std::vector<PlaceStep>& path, const Expr* index_term) {
    for (std::size_t index = locals.size(); index > 0; --index) {
        const Local& candidate = locals[index - 1];
        if (candidate.pointer != std::optional{pointer} || candidate.pointer_version != version ||
            candidate.path != path) {
            continue;
        }
        if (index_term != nullptr &&
            (candidate.index_value.empty() || !same_term(candidate.index_value.front(), *index_term))) {
            continue;
        }
        return index - 1;
    }
    return std::nullopt;
}

// The tracked place an access names, if it is storage this body tracks.
//
// A dereference is not looked up by declaration: it is identified by the
// pointer and the version whose value it reads.
std::optional<std::size_t> tracked_place(CXCursor cursor, const Locals& locals, const Signature& signature) {
    const auto access = resolve_access(cursor);
    if (!access)
        return std::nullopt;
    const auto declaration = access->declaration;
    // A symbolic subscript names the storage its index selects, so the entry is
    // found by that term as well as by the path. Reading it here costs a second
    // lowering of the index and is what keeps `a[i]` and `a[j]` apart.
    std::optional<Expr> index_term;
    if (!access->path.empty() && access->path.back().kind == PlaceStep::Kind::SymbolicElement &&
        !access->symbolic_indices.empty()) {
        index_term = build_expression(access->symbolic_indices.back(), signature, locals, 0, false);
    }
    if (!access->dereferenced) {
        return find_local(locals, declaration, access->path, index_term ? &*index_term : nullptr);
    }
    // A dereference entry records the pointer it came from, so it is found by
    // matching that pointer's declaration rather than by looking the pointer up
    // as tracked storage: a pointer parameter is not itself a modeled value.
    for (std::size_t index = locals.size(); index > 0; --index) {
        const Local& candidate = locals[index - 1];
        if (!candidate.is_deref() || clang_equalCursors(candidate.declaration, declaration) == 0 ||
            candidate.path != access->path) {
            continue;
        }
        if (candidate.has_symbolic_step() &&
            (!index_term || candidate.index_value.empty() || !same_term(candidate.index_value.front(), *index_term))) {
            continue;
        }
        return index - 1;
    }
    return std::nullopt;
}

// The pointer a dereference place records for pointer `pointer`, and the
// version of it the place reads through: the tracked local holding it, or the
// parameter at its position, which is never written (`resolve_storage`).
std::optional<std::pair<std::size_t, std::uint32_t>> pointer_root(const Locals& locals,
                                                                  const std::vector<CXCursor>& parameters,
                                                                  CXCursor pointer) {
    if (const std::optional<std::size_t> tracked = find_local(locals, pointer)) {
        return std::pair{*tracked, locals[*tracked].version};
    }
    const auto at = std::ranges::find_if(
        parameters, [&](CXCursor candidate) { return clang_equalCursors(candidate, pointer) != 0; });
    if (at == parameters.end()) {
        return std::nullopt;
    }
    return std::pair{static_cast<std::size_t>(at - parameters.begin()), std::uint32_t{0}};
}

// The dereference place of the object pointer `pointer` designates, at `path`
// within it, formed earlier through the pointer's current version.
std::optional<std::size_t> pointee_place(const Locals& locals, const std::vector<CXCursor>& parameters,
                                         CXCursor pointer, const std::vector<PlaceStep>& path) {
    const auto root = pointer_root(locals, parameters, pointer);
    if (!root) {
        return std::nullopt;
    }
    return find_deref(locals, root->first, root->second, path, nullptr);
}

// The one read of tracked storage (SPEC.md 12.10, RFC 0014 §17 step 2).
//
// A read resolves a place to the version current where the read stands, and
// denotes the value that version was given. Every access form - a local, a
// member, an element, a reference's referent - reads through here, so no syntax
// gets a read rule of its own and a fact can never be attached to a spelling
// instead of to a version.
Expr read_place(const Locals& locals, std::size_t entry, CXCursor at) {
    Expr expr;
    expr.type = locals[entry].type;
    expr.location = presumed_location(clang_getCursorLocation(at));
    expr.node = PlaceRef{locals[entry].version, place_of(locals, entry)};
    return expr;
}

} // namespace cppl::clangbridge::detail
