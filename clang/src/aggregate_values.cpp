#include "aggregate_values.hpp"

#include "cppl/clang/ast.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
#include "places.hpp"

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

// Reading a struct value whole, and deciding which copies of one are the value
// they copy (TRUST.md TCB-AGGREGATE-001, TCB-AGGREGATE-002).
namespace cppl::clangbridge::detail::aggregates {

using bridge::children_of;
using bridge::convert_type;
using bridge::designated_object;
using bridge::find_binding;
using bridge::passing_of;
using bridge::presumed_location;
using bridge::read_place;
using bridge::record_fields;
using bridge::record_has_base;
using bridge::resolve_access;
using bridge::same_modeled_value;
using bridge::stale_borrow;
using bridge::strip_parens;
using bridge::take;
using bridge::unsupported_expression;

namespace {

// Whether this body tracks a place strictly inside the object `declaration`
// designates at `prefix`: the object is then storage of one place per scalar
// leaf rather than one place of its own.
bool tracked_inside(const Locals& locals, CXCursor declaration, const std::vector<PlaceStep>& prefix) {
    return std::ranges::any_of(locals, [&](const Local& entry) {
        return !entry.is_deref() && !entry.referent.has_value() && !entry.binder.has_value() &&
               clang_equalCursors(entry.declaration, declaration) != 0 && entry.path.size() > prefix.size() &&
               std::equal(prefix.begin(), prefix.end(), entry.path.begin());
    });
}

// How a member or an element of `whole` named `name` is written.
std::string member_spelling(const std::string& whole, const std::string& name, bool element) {
    return element ? whole + "[" + name + "]" : whole + "." + name;
}

// Why a value of `whole` cannot be assembled where its member `member` is not
// one of the places this body tracks.
std::string untracked_member(const std::string& member, const std::string& whole) {
    return "'" + member + "' is not tracked where '" + whole + "' is read, so '" + whole +
           "' is not one value this body can state";
}

// The value the object `declaration` designates at `path` holds where `at`
// stands, assembled from the places this body tracks it as: one operand per
// component, in component order, each a leaf's current version or, for a member
// that is itself a record or an array, its own assembly (TRUST.md
// TCB-AGGREGATE-001). A leaf of a by-value parameter this body does not track is
// one it never writes, so it holds the parameter's member as passed. Any other
// leaf must be tracked here, and one that is not is named rather than left out:
// a member left out would be one nothing states while the value still claims to
// be the object's.
Expr assemble_object(const Locals& locals, const Frame& frame, CXCursor declaration, const std::vector<PlaceStep>& path,
                     const Type& type, const std::string& spelling, CXCursor at, unsigned depth = 0) {
    if (!structural(type) || path.size() >= kMaxPlaceDepth || depth > kMaxPlaceDepth) {
        return unsupported_expression(at, "'" + spelling + "' has type '" + type.spelling +
                                              "', which this body does not track member by member");
    }
    const std::optional<std::uint32_t> parameter =
        passing_of(clang_getCursorType(declaration)) == source::ParameterPassing::Value ? frame.position_of(declaration)
                                                                                        : std::nullopt;
    const bool array = type.representation.kind != source::RepresentationKind::Record;
    Aggregate assembled;
    for (std::size_t member = 0; member < type.representation.components.size(); ++member) {
        std::vector<PlaceStep> reached = path;
        reached.push_back(
            PlaceStep{array ? PlaceStep::Kind::Element : PlaceStep::Kind::Field, static_cast<std::uint32_t>(member)});
        const std::string& name = type.representation.components[member].name;
        const std::string written = member_spelling(spelling, name, array);
        const Type& member_type = type.projections[member];
        if (member_type.kind == TypeKind::Value) {
            Expr nested = assemble_object(locals, frame, declaration, reached, member_type, written, at, depth + 1);
            if (std::holds_alternative<Unsupported>(nested.node)) {
                return nested;
            }
            assembled.operands.push_back(std::move(nested));
            continue;
        }
        const std::optional<std::size_t> binding = find_binding(locals, declaration, reached);
        if (!binding.has_value() && parameter.has_value()) {
            Expr passed;
            passed.type = convert_type(clang_getCursorType(declaration));
            passed.location = presumed_location(clang_getCursorLocation(at));
            passed.node = ParameterRef{*parameter, take(clang_getCursorSpelling(declaration))};
            std::optional<Expr> projected = member_at(std::move(passed), reached);
            if (!projected.has_value() || !same_modeled_value(member_type, projected->type)) {
                return unsupported_expression(at, "'" + written + "' is not a member of parameter '" +
                                                      take(clang_getCursorSpelling(declaration)) +
                                                      "' this implementation can state");
            }
            assembled.operands.push_back(std::move(*projected));
            continue;
        }
        if (!binding.has_value() || locals[*binding].referent.has_value() || locals[*binding].binder.has_value() ||
            !same_modeled_value(member_type, locals[*binding].type)) {
            return unsupported_expression(at, untracked_member(written, spelling));
        }
        if (std::optional<std::string> stale = stale_borrow(locals, *binding)) {
            return unsupported_expression(at, std::move(*stale));
        }
        assembled.operands.push_back(read_place(locals, *binding, at));
    }
    Expr expr;
    expr.type = type;
    expr.location = presumed_location(clang_getCursorLocation(at));
    expr.node = std::move(assembled);
    return expr;
}

// Whether `cursor` constructs an object by calling one of its class's
// constructors: a copy, a move, a temporary formed with parentheses, a
// conversion. Clang reports every such construction as a call of the
// constructor.
bool constructs(CXCursor cursor) {
    return clang_getCursorKind(cursor) == CXCursor_CallExpr &&
           clang_getCursorKind(clang_getCursorReferenced(cursor)) == CXCursor_Constructor;
}

} // namespace

std::optional<std::uint32_t> Frame::position_of(CXCursor declaration) const {
    if (parameters == nullptr) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < parameters->size(); ++index) {
        if (clang_equalCursors((*parameters)[index], declaration) != 0) {
            return leaves + static_cast<std::uint32_t>(index);
        }
    }
    return std::nullopt;
}

// Only such a value is assembled from its members, and only such a value's copy
// is read as the value it copies (TRUST.md TCB-AGGREGATE-001).
bool structural(const Type& type) {
    using K = source::RepresentationKind;
    return type.kind == TypeKind::Value &&
           (type.representation.kind == K::Record || type.representation.kind == K::Array ||
            type.representation.kind == K::StdArray) &&
           type.representation.rejection.empty() && !type.representation.components.empty() &&
           type.projections.size() == type.representation.components.size();
}

// These are the places a body tracks such an object as (SPEC.md 12.10).
bool leaf_paths(const Type& type, const std::vector<PlaceStep>& prefix, std::vector<std::vector<PlaceStep>>& paths) {
    if (!structural(type) || prefix.size() >= kMaxPlaceDepth) {
        return false;
    }
    const bool array = type.representation.kind != source::RepresentationKind::Record;
    for (std::size_t member = 0; member < type.projections.size(); ++member) {
        if (paths.size() >= kMaxTrackedLeaves) {
            return false;
        }
        std::vector<PlaceStep> reached = prefix;
        reached.push_back(
            PlaceStep{array ? PlaceStep::Kind::Element : PlaceStep::Kind::Field, static_cast<std::uint32_t>(member)});
        const Type& member_type = type.projections[member];
        if (member_type.kind == TypeKind::Value) {
            if (!leaf_paths(member_type, reached, paths)) {
                return false;
            }
            continue;
        }
        if (member_type.kind == TypeKind::Unsupported) {
            return false;
        }
        paths.push_back(std::move(reached));
    }
    return true;
}

std::optional<Expr> member_at(Expr whole, const std::vector<PlaceStep>& path) {
    for (const PlaceStep& step : path) {
        if (step.kind == PlaceStep::Kind::SymbolicElement) {
            return std::nullopt;
        }
        if (auto* assembled = std::get_if<Aggregate>(&whole.node)) {
            if (step.index >= assembled->operands.size()) {
                return std::nullopt;
            }
            Expr operand = std::move(assembled->operands[step.index]);
            whole = std::move(operand);
            continue;
        }
        if (step.index >= whole.type.projections.size() || !whole.type.representation.rejection.empty()) {
            return std::nullopt;
        }
        Expr component;
        component.type = whole.type.projections[step.index];
        component.location = whole.location;
        component.node = Projection{step.index, {std::move(whole)}};
        whole = std::move(component);
    }
    return whole;
}

// Its name, then each member or element the path selects; storage rooted in the
// implicit object is spelled through `this`.
std::string spelled_access(CXCursor declaration, const std::vector<PlaceStep>& path) {
    const CXCursorKind kind = clang_getCursorKind(declaration);
    const bool receiver = kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl;
    std::string text = receiver ? (path.empty() ? "*this" : "this") : take(clang_getCursorSpelling(declaration));
    Type current = convert_type(clang_getCursorType(declaration), 0, ReferenceModel::Referent);
    for (const PlaceStep& step : path) {
        const auto& components = current.representation.components;
        if (step.kind == PlaceStep::Kind::SymbolicElement || step.index >= components.size() ||
            step.index >= current.projections.size()) {
            return text;
        }
        const bool through_this = receiver && text == "this";
        text += current.representation.kind == source::RepresentationKind::Record
                    ? (through_this ? "->" : ".") + components[step.index].name
                    : "[" + components[step.index].name + "]";
        Type next = current.projections[step.index];
        current = std::move(next);
    }
    return text;
}

// An aggregate local, or a by-value aggregate parameter the body writes, is read
// as the value its leaves hold here, not as the one it was declared or passed
// with.
std::optional<Expr> object_value(CXCursor cursor, CXCursor declaration, const Locals& locals, const Frame& frame) {
    const Type whole = convert_type(clang_getCursorType(cursor));
    if (!structural(whole) || !tracked_inside(locals, declaration, {})) {
        return std::nullopt;
    }
    return assemble_object(locals, frame, declaration, {}, whole, take(clang_getCursorSpelling(declaration)), cursor);
}

std::optional<Expr> member_value(CXCursor cursor, const Locals& locals, const Frame& frame) {
    const Type type = convert_type(clang_getCursorType(cursor));
    if (!structural(type)) {
        return std::nullopt;
    }
    const std::optional<ResolvedAccess> access = resolve_access(cursor);
    if (!access || access->dereferenced || access->receiver || !access->symbolic_indices.empty() ||
        !tracked_inside(locals, access->declaration, access->path)) {
        return std::nullopt;
    }
    return assemble_object(locals, frame, access->declaration, access->path, type,
                           spelled_access(access->declaration, access->path), cursor);
}

// A body tracks every leaf of its implicit object, so `*this`, and a member of
// it that is itself a record or an array, is read whole as its leaves assemble
// it. A clause reads each leaf as the parameter it is, and is not asked.
std::optional<Expr> receiver_value(CXCursor cursor, const ResolvedAccess& access, const Locals& locals,
                                   const Frame& frame) {
    const Type whole = convert_type(clang_getCursorType(cursor));
    if (!structural(whole) || !access.symbolic_indices.empty() || clang_Cursor_isNull(frame.receiver) != 0 ||
        clang_equalCursors(access.declaration, frame.receiver) == 0 ||
        !tracked_inside(locals, access.declaration, access.path)) {
        return std::nullopt;
    }
    return assemble_object(locals, frame, access.declaration, access.path, whole,
                           spelled_access(access.declaration, access.path), cursor);
}

// A copy that runs only what C++ defines copies each member as it is (C++
// [class.copy.ctor], [class.copy.assign]), which is the value copied; one that
// runs anything else does whatever that code does. A constructor or assignment
// template a copy could select instead is a member template the proof-only text
// instantiates, which refuses the unit before any body is lowered (SPEC.md
// ERASE-019, TRUST.md TCB-AGGREGATE-002).
std::optional<std::string> user_provided_copy(CXType type, Copying copying, unsigned depth) {
    const CXType canonical = clang_getCanonicalType(type);
    if (depth > kMaxPlaceDepth) {
        return "'" + take(clang_getTypeSpelling(canonical)) + "' nests deeper than this implementation follows";
    }
    if (canonical.kind == CXType_ConstantArray) {
        return user_provided_copy(clang_getArrayElementType(canonical), copying, depth + 1);
    }
    if (canonical.kind != CXType_Record) {
        return std::nullopt;
    }
    const std::string name = take(clang_getTypeSpelling(canonical));
    const CXCursor definition = clang_getCursorDefinition(clang_getTypeDeclaration(canonical));
    if (clang_Cursor_isNull(definition) != 0) {
        return "'" + name + "' is incomplete";
    }
    if (record_has_base(canonical)) {
        return "'" + name + "' has a base class, whose copy is not modeled";
    }
    const bool assigning = copying == Copying::Assignment;
    const CXCursorKind special = assigning ? CXCursor_CXXMethod : CXCursor_Constructor;
    for (const CXCursor member : children_of(definition)) {
        if (clang_getCursorKind(member) != special || clang_CXXMethod_isDeleted(member) != 0) {
            continue;
        }
        const bool copy = assigning ? clang_CXXMethod_isCopyAssignmentOperator(member) != 0
                                    : clang_CXXConstructor_isCopyConstructor(member) != 0;
        const bool move = assigning ? clang_CXXMethod_isMoveAssignmentOperator(member) != 0
                                    : clang_CXXConstructor_isMoveConstructor(member) != 0;
        if ((copy || move) && clang_CXXMethod_isDefaulted(member) == 0) {
            return "the " + std::string(copy ? "copy" : "move") +
                   (assigning ? " assignment operator" : " constructor") + " of '" + name + "' is user-provided, so " +
                   (copy ? "a copy" : "a move") +
                   " of it runs code of the program whose effect on the value is not modeled";
        }
    }
    for (const CXCursor field : record_fields(canonical)) {
        if (std::optional<std::string> inner = user_provided_copy(clang_getCursorType(field), copying, depth + 1)) {
            return inner;
        }
    }
    return std::nullopt;
}

// `T{a, b}` initializes a `T` exactly as `{a, b}` does.
CXCursor braced_list(CXCursor initializer) {
    if (clang_Cursor_isNull(initializer) != 0) {
        return clang_getNullCursor();
    }
    const CXCursor stripped = strip_parens(initializer);
    if (clang_getCursorKind(stripped) == CXCursor_InitListExpr) {
        return stripped;
    }
    if (clang_getCursorKind(stripped) == CXCursor_CXXFunctionalCastExpr) {
        for (const CXCursor child : children_of(stripped)) {
            if (clang_getCursorKind(child) == CXCursor_InitListExpr &&
                same_modeled_value(convert_type(clang_getCursorType(stripped)),
                                   convert_type(clang_getCursorType(child)))) {
                return child;
            }
        }
    }
    return clang_getNullCursor();
}

// The constructor is a copy or move constructor C++ defines, of a type tracked
// member by member whose members' are all defined by C++ as well. Such a copy
// is the value it copies, member for member, so it is read as that value (C++
// [class.copy.ctor], TRUST.md TCB-AGGREGATE-001).
std::expected<CXCursor, std::string> copied_operand(CXCursor construction) {
    const CXCursor constructor = clang_getCursorReferenced(construction);
    const CXType constructed = clang_getCursorType(construction);
    const std::string name = take(clang_getTypeSpelling(clang_getCanonicalType(constructed)));
    const bool copy = clang_CXXConstructor_isCopyConstructor(constructor) != 0;
    const bool move = clang_CXXConstructor_isMoveConstructor(constructor) != 0;
    if ((!copy && !move) || clang_Cursor_getNumArguments(construction) != 1) {
        return std::unexpected("constructing '" + name +
                               "' runs a constructor that is not modeled: a value of a class is formed in a verified "
                               "body only by an aggregate initializer, a copy or a move (SPEC.md CLASS-015)");
    }
    const std::string what = copy ? "copying" : "moving";
    const Type type = convert_type(constructed);
    if (!structural(type)) {
        return std::unexpected(what + " '" + name +
                               "' is not modeled: only a record or an array whose members are all modeled is copied "
                               "or moved in a verified body");
    }
    // The constructor selected is one of the class's own copy or move
    // constructors, which this checks with every member's.
    if (std::optional<std::string> user = user_provided_copy(constructed)) {
        return std::unexpected(*user);
    }
    const CXCursor operand = designated_object(clang_Cursor_getArgument(construction, 0));
    const Type copied = convert_type(clang_getCursorType(operand));
    if (!same_modeled_value(type, copied)) {
        return std::unexpected(what + " '" + name + "' from a value of type '" + copied.spelling + "' is not modeled");
    }
    return operand;
}

// Nothing but a constructor call is ever stepped through, so a conversion Clang
// recorded is never discarded here.
CXCursor copied_value(CXCursor cursor) {
    CXCursor current = cursor;
    for (unsigned depth = 0; depth < kMaxExpressionDepth; ++depth) {
        const CXCursor stripped = strip_parens(current);
        if (!constructs(stripped)) {
            return current;
        }
        const std::expected<CXCursor, std::string> operand = copied_operand(stripped);
        if (!operand) {
            return current;
        }
        current = *operand;
    }
    return current;
}

// A copy or move constructor the program provides is refused where such a value
// is lowered, by name; any other constructor leaves the members holding what
// that code made them hold, and is no whole value.
bool initializes_whole(CXCursor initializer) {
    if (clang_Cursor_isNull(initializer) != 0 || clang_Cursor_isNull(braced_list(initializer)) == 0) {
        return false;
    }
    const CXCursor stripped = strip_parens(initializer);
    const CXCursor constructor = clang_getCursorReferenced(stripped);
    return !constructs(stripped) || clang_CXXConstructor_isCopyConstructor(constructor) != 0 ||
           clang_CXXConstructor_isMoveConstructor(constructor) != 0;
}

// One C++ defines memberwise is lowered as each member's write; one the program
// provides is refused where the statement is lowered.
bool assigns_whole(CXCursor statement) {
    const CXCursor method = clang_getCursorReferenced(statement);
    return clang_getCursorKind(method) == CXCursor_CXXMethod && clang_Cursor_getNumArguments(statement) == 2 &&
           (clang_CXXMethod_isCopyAssignmentOperator(method) != 0 ||
            clang_CXXMethod_isMoveAssignmentOperator(method) != 0);
}

} // namespace cppl::clangbridge::detail::aggregates
