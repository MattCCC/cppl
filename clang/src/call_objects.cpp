#include "call_objects.hpp"

#include "access.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/storage.hpp"
#include "places.hpp"
#include "signature.hpp"
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

// The object a member function call is made on (SPEC.md CLASS-011): which
// expression designates it, whether it forms a new value, the place the caller
// can name it by, and what the call passes for each place of the callee's
// implicit object.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::strip_parens;
using bridge::take;

namespace {

// Whether `function` is declared in namespace `std`, directly or in one of the
// inline namespaces a library puts its declarations in.
bool declared_in_std(CXCursor function) {
    std::string outermost;
    CXCursor parent = clang_getCursorSemanticParent(function);
    for (unsigned depth = 0; depth < kMaxExpressionDepth && clang_getCursorKind(parent) == CXCursor_Namespace;
         ++depth) {
        outermost = take(clang_getCursorSpelling(parent));
        parent = clang_getCursorSemanticParent(parent);
    }
    return outermost == "std" && clang_getCursorKind(parent) == CXCursor_TranslationUnit;
}

// Whether the cast `expression` converts to an rvalue reference type:
// `static_cast<T&&>(o)`. Clang reports such a cast's type without the
// reference, as the type of the xvalue it forms, so the written target is read
// from the cast's own tokens: the `&&` closing its template argument.
bool casts_to_rvalue_reference(CXCursor expression) {
    CXTranslationUnit unit = clang_Cursor_getTranslationUnit(expression);
    CXToken* tokens = nullptr;
    unsigned count = 0;
    clang_tokenize(unit, clang_getCursorExtent(expression), &tokens, &count);
    bool rvalue = false;
    for (unsigned index = 1; index < count; ++index) {
        if (take(clang_getTokenSpelling(unit, tokens[index])) == ">") {
            rvalue = take(clang_getTokenSpelling(unit, tokens[index - 1])) == "&&";
            break;
        }
    }
    clang_disposeTokens(unit, tokens, count);
    return rvalue;
}

} // namespace

// Whether a call runs the function a pointer to member designates:
// `(object.*f)()` or `(pointer->*f)()`. Which function that is, is a value.
bool calls_through_member_pointer(CXCursor call) {
    const std::vector<CXCursor> children = children_of(call);
    if (children.empty()) {
        return false;
    }
    const CXCursor callee = strip_parens(children.front());
    if (clang_getCursorKind(callee) != CXCursor_BinaryOperator) {
        return false;
    }
    const enum CXBinaryOperatorKind op = clang_getCursorBinaryOperatorKind(callee);
    return op == CXBinaryOperator_PtrMemD || op == CXBinaryOperator_PtrMemI;
}

// Whether `expression` forms a new value rather than naming storage, so that a
// reference parameter bound to it binds a temporary no one names after the
// call. Only a form that never designates existing storage counts; any other is
// taken to name storage, so nothing that does is mistaken for a temporary.
bool is_prvalue(CXCursor expression) {
    expression = strip_parens(expression);
    switch (clang_getCursorKind(expression)) {
        case CXCursor_IntegerLiteral:
        case CXCursor_CharacterLiteral:
        case CXCursor_FloatingLiteral:
        case CXCursor_CXXBoolLiteralExpr:
        case CXCursor_CXXNullPtrLiteralExpr:
            return true;
        case CXCursor_BinaryOperator:
            switch (clang_getCursorBinaryOperatorKind(expression)) {
                case CXBinaryOperator_Assign:
                case CXBinaryOperator_MulAssign:
                case CXBinaryOperator_DivAssign:
                case CXBinaryOperator_RemAssign:
                case CXBinaryOperator_AddAssign:
                case CXBinaryOperator_SubAssign:
                case CXBinaryOperator_ShlAssign:
                case CXBinaryOperator_ShrAssign:
                case CXBinaryOperator_AndAssign:
                case CXBinaryOperator_XorAssign:
                case CXBinaryOperator_OrAssign:
                case CXBinaryOperator_Comma:
                case CXBinaryOperator_PtrMemD:
                case CXBinaryOperator_PtrMemI:
                case CXBinaryOperator_Invalid:
                    return false;
                default:
                    return true;
            }
        case CXCursor_UnaryOperator:
            switch (clang_getCursorUnaryOperatorKind(expression)) {
                case CXUnaryOperator_Deref:
                case CXUnaryOperator_PreInc:
                case CXUnaryOperator_PreDec:
                case CXUnaryOperator_Real:
                case CXUnaryOperator_Imag:
                case CXUnaryOperator_Extension:
                case CXUnaryOperator_Coawait:
                case CXUnaryOperator_Invalid:
                    return false;
                default:
                    return true;
            }
        case CXCursor_CallExpr: {
            const CXCursor function = clang_getCursorReferenced(expression);
            if (clang_Cursor_isNull(function) != 0) {
                return false;
            }
            const CXTypeKind returned = clang_getCanonicalType(clang_getCursorResultType(function)).kind;
            return returned != CXType_LValueReference && returned != CXType_RValueReference &&
                   returned != CXType_Invalid;
        }
        default:
            return false;
    }
}

// The object an expression that names an object as an rvalue designates:
// `std::move(o)`, `std::forward<T>(o)` and `static_cast<T&&>(o)` each designate
// `o` itself, so a member function called on one is called on `o`'s storage.
// A ref-qualifier decides which of these a call may be made on, and Clang has
// checked it (SPEC.md CLASS-009, CLASS-011). Any other expression is returned
// as it is: a temporary is a new object, not the one it was made from.
CXCursor designated_object(CXCursor expression) {
    expression = strip_parens(expression);
    const CXCursorKind kind = clang_getCursorKind(expression);
    if (kind == CXCursor_CallExpr && clang_Cursor_getNumArguments(expression) == 1) {
        const CXCursor function = clang_getCursorReferenced(expression);
        const std::string name = take(clang_getCursorSpelling(function));
        if ((name == "move" || name == "forward") && declared_in_std(function)) {
            return strip_parens(clang_Cursor_getArgument(expression, 0));
        }
    }
    if (kind == CXCursor_CXXStaticCastExpr && casts_to_rvalue_reference(expression)) {
        for (const CXCursor child : children_of(expression)) {
            if (clang_isExpression(clang_getCursorKind(child)) != 0) {
                return strip_parens(child);
            }
        }
    }
    return expression;
}

// The object `call`, a call of non-static member function `callee`, is made on.
// Only an object this implementation can name as storage qualifies: the
// caller's implicit object, a local or a parameter, or a member or an element
// of one at a constant. An object reached through a pointer, a temporary, or
// the base subobject of another object is refused, since the callee's implicit
// object would then be storage the caller does not track.
std::expected<CallObject, std::string> call_object(CXCursor call, CXCursor callee) {
    const std::vector<CXCursor> children = children_of(call);
    const CXCursor callee_record = enclosing_record(callee);
    const std::string base_class =
        "this call converts its object to a base class, and a base subobject is not modeled (SPEC.md CLASS-015)";
    if (children.empty() || clang_getCursorKind(children.front()) != CXCursor_MemberRefExpr) {
        return std::unexpected("call does not resolve to an ordinary function or to a member function named on its "
                               "object; an overloaded operator and a lambda's call operator are not modeled");
    }
    const std::vector<CXCursor> member = children_of(children.front());
    if (member.empty()) {
        // `m()` alone is `this->m()`: the caller's own implicit object.
        return CallObject{callee_record, {}, true};
    }
    if (member.size() != 1) {
        return std::unexpected("the object of this call was not resolved");
    }
    if (const std::optional<CXCursor> record = this_record(member.front())) {
        if (clang_equalCursors(*record, callee_record) == 0) {
            return std::unexpected(base_class);
        }
        return CallObject{*record, {}, true};
    }
    const CXCursor written = designated_object(member.front());
    const auto access = resolve_access(written);
    if (!access.has_value()) {
        return std::unexpected("the object of this call is not storage this implementation can name, such as a "
                               "local, a parameter, a member of one, or what a pointer parameter designates");
    }
    // `p->f()` names the object `p` designates without writing a `*`: the
    // member access dereferences a pointer-typed operand.
    CXType object_type = clang_getCanonicalType(clang_getCursorType(written));
    const bool arrow = object_type.kind == CXType_Pointer;
    if (arrow) {
        object_type = clang_getCanonicalType(clang_getPointeeType(object_type));
    }
    if (!access->symbolic_indices.empty()) {
        return std::unexpected("a member function called on an element selected at a term is not modeled: this "
                               "implementation forms no place for a member of such an element (SPEC.md CLASS-015)");
    }
    const bool through_pointer = arrow || access->dereferenced;
    // The pointer must be one the contract names, a parameter, since a
    // capability is stated about a parameter; the object it designates is then
    // a place, reached under that capability (SPEC.md CLASS-011, VERIFIED-038).
    if (through_pointer && (clang_getCursorKind(access->declaration) != CXCursor_ParmDecl ||
                            (arrow && (!access->path.empty() || access->dereferenced)))) {
        return std::unexpected("a member function called through a pointer is modeled only where the pointer is a "
                               "parameter a capability is stated for");
    }
    const CXCursor object_record = clang_getCanonicalCursor(clang_getTypeDeclaration(object_type));
    if (clang_equalCursors(object_record, callee_record) == 0) {
        return std::unexpected(base_class);
    }
    return CallObject{access->declaration, access->path, access->receiver, through_pointer};
}

// The place of the object a member call is made on, at `path` within the
// caller's storage: tracked storage of the caller, or the dereference place the
// statement formed for an object a pointer designates (SPEC.md CLASS-011).
std::optional<std::size_t> object_place(const Locals& locals, const std::vector<CXCursor>& parameters,
                                        const CallObject& object, const std::vector<PlaceStep>& path) {
    return object.through_pointer ? pointee_place(locals, parameters, object.declaration, path)
                                  : find_local(locals, object.declaration, path);
}

// What a member call passes for one leaf of the callee's implicit object: the
// caller's own storage at `path`, read at its current version, or in a clause
// the caller's leaf standing there (SPEC.md CLASS-011).
Expr receiver_argument(const CallObject& object, const std::vector<PlaceStep>& path, const ReceiverLeaf& leaf,
                       const Signature& signature, const Locals& locals, CXCursor at) {
    if (signature.clause) {
        if (!object.receiver || !signature.receiver.has_value() ||
            clang_equalCursors(object.declaration, signature.receiver->record) == 0) {
            return unsupported_expression(at, "a clause calls a member function only on the implicit object");
        }
        const std::optional<std::size_t> index = signature.receiver->leaf_at(path);
        if (!index.has_value()) {
            return unsupported_expression(at, "the implicit object has no tracked member where the callee's '" +
                                                  leaf.spelling + "' stands");
        }
        Expr expr;
        expr.type = signature.receiver->leaves[*index].type;
        expr.type.refinements.clear(); // a read names the value, not the storage's type
        expr.location = presumed_location(clang_getCursorLocation(at));
        expr.node = ParameterRef{static_cast<std::uint32_t>(*index), signature.receiver->leaves[*index].spelling};
        return expr;
    }
    if (const std::optional<std::size_t> entry = object_place(locals, signature.parameters, object, path)) {
        return read_place(locals, *entry, at);
    }
    if (object.through_pointer) {
        return unsupported_expression(at, "the place the callee's '" + leaf.spelling +
                                              "' stands at in the object this call is made on was not formed");
    }
    // An object this body reads as one whole value is projected to the leaf:
    // one a parameter designates by reference, at the value its place holds
    // where the call is made, and a parameter passed by value that is not
    // tracked member by member, at the value it arrived with -- nothing writes
    // that one, since a write to it is refused and an unsafe block that may
    // change it refuses the body (SPEC.md UNSAFE-005).
    std::optional<Expr> whole_value;
    std::size_t from = 0;
    if (const std::optional<std::size_t> whole = find_local(locals, object.declaration);
        whole.has_value() && locals[*whole].read_only) {
        whole_value = read_place(locals, *whole, at);
        from = locals[*whole].path.size();
    } else if (const std::optional<std::uint32_t> position = signature.position_of(object.declaration);
               position.has_value() && !object.receiver &&
               passing_of(clang_getCursorType(object.declaration)) == source::ParameterPassing::Value &&
               std::ranges::none_of(locals, [&](const Local& entry) {
                   return clang_equalCursors(entry.declaration, object.declaration) != 0;
               })) {
        Expr parameter;
        parameter.type = convert_type(clang_getCursorType(object.declaration));
        parameter.location = presumed_location(clang_getCursorLocation(at));
        parameter.node = ParameterRef{*position, take(clang_getCursorSpelling(object.declaration))};
        whole_value = std::move(parameter);
    }
    if (whole_value.has_value() && path.size() >= from) {
        Expr value = std::move(*whole_value);
        for (std::size_t step = from; step < path.size(); ++step) {
            const PlaceStep& taken = path[step];
            if (taken.kind == PlaceStep::Kind::SymbolicElement || !value.type.representation.rejection.empty() ||
                taken.index >= value.type.projections.size()) {
                return unsupported_expression(at, "the object of this call has a member this body cannot state where "
                                                  "the callee's '" +
                                                      leaf.spelling + "' stands");
            }
            Expr component;
            component.type = value.type.projections[taken.index];
            component.location = value.location;
            component.node = Projection{taken.index, {std::move(value)}};
            value = std::move(component);
        }
        return value;
    }
    return unsupported_expression(at, "the object of this call has storage this body does not track where the "
                                      "callee's '" +
                                          leaf.spelling + "' stands");
}

} // namespace cppl::clangbridge::detail
