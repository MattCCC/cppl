#include "expressions.hpp"

#include "access.hpp"
#include "aggregate_values.hpp"
#include "call_objects.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
#include "default_arguments.hpp"
#include "places.hpp"
#include "sequences.hpp"
#include "signature.hpp"
#include "types.hpp"
#include "unsafe.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// The lowering of an expression of a verified body or a clause into the term it
// denotes (SPEC.md 12.8): every form this implementation models is read here,
// through the one read of tracked storage, and every other is refused naming
// what the author wrote.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::strip_parens;
using bridge::take;

namespace {

// Names what the author wrote, not Clang's class for it.
std::string unmodeled_expression(CXCursor cursor, CXCursorKind kind) {
    switch (kind) {
        case CXCursor_UnaryOperator:
            // A dereference is not merely an unmodeled operator. It awaits the
            // memory-validity obligations of RFC 0014: `p != nullptr` is
            // necessary and insufficient for a valid dereference, and the
            // pointer's state model may never supply the difference.
            if (clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_Deref)
                return "dereferencing a pointer requires the memory-validity obligations of RFC 0014, which are not "
                       "implemented; 'p != nullptr' alone does not establish that 'p' may be dereferenced";
            return "operator '" + take(clang_getUnaryOperatorKindSpelling(clang_getCursorUnaryOperatorKind(cursor))) +
                   "' is not modeled";
        case CXCursor_CStyleCastExpr:
        case CXCursor_CXXFunctionalCastExpr:
        case CXCursor_CXXStaticCastExpr:
        case CXCursor_CXXConstCastExpr:
        case CXCursor_CXXReinterpretCastExpr:
        case CXCursor_CXXDynamicCastExpr:
            return "an explicit conversion is not modeled";
        case CXCursor_FloatingLiteral:
            return "floating-point values are not modeled";
        case CXCursor_CXXBoolLiteralExpr:
            return "'bool' literals are not modeled";
        case CXCursor_MemberRefExpr:
            return "member access is not modeled";
        case CXCursor_ArraySubscriptExpr:
            return "subscripting is not modeled";
        case CXCursor_CXXThisExpr:
            return "'this' is not modeled";
        case CXCursor_CXXNewExpr:
        case CXCursor_CXXDeleteExpr:
            return "dynamic allocation is not modeled";
        case CXCursor_CXXThrowExpr:
            return "exceptions are not modeled";
        default:
            return "'" + take(clang_getCursorKindSpelling(kind)) + "' is not modeled";
    }
}

Expr build_integer_literal(CXCursor cursor) {
    CXEvalResult evaluated = clang_Cursor_Evaluate(cursor);
    if (evaluated == nullptr) {
        return unsupported_expression(cursor, "Clang could not evaluate this literal");
    }

    struct Release {
        CXEvalResult result;
        ~Release() {
            clang_EvalResult_dispose(result);
        }
    } release{evaluated};

    if (clang_EvalResult_getKind(evaluated) != CXEval_Int) {
        return unsupported_expression(cursor, "literal does not evaluate to an integer");
    }

    std::int64_t value = 0;
    if (clang_EvalResult_isUnsignedInt(evaluated) != 0) {
        const unsigned long long unsigned_value = clang_EvalResult_getAsUnsigned(evaluated);
        value = static_cast<std::int64_t>(unsigned_value);
    } else {
        value = static_cast<std::int64_t>(clang_EvalResult_getAsLongLong(evaluated));
    }

    Expr expr;
    expr.type = convert_type(clang_getCursorType(cursor));
    expr.location = presumed_location(clang_getCursorLocation(cursor));
    expr.node = IntLiteral{value};
    return expr;
}

} // namespace

Expr build_expression(CXCursor cursor, const Signature& signature, const Locals& locals, unsigned depth,
                      bool sequenced_call) {
    if (depth > kMaxExpressionDepth) {
        return unsupported_expression(cursor, "expression nests deeper than the bridge allows");
    }

    const CXCursorKind kind = clang_getCursorKind(cursor);

    const auto make_projection = [&](Expr subject, std::uint32_t index) -> Expr {
        if (index >= subject.type.projections.size())
            return unsupported_expression(cursor, "logical projection is outside the resolved signature");
        Expr result;
        result.type = subject.type.projections[index];
        result.location = presumed_location(clang_getCursorLocation(cursor));
        result.node = Projection{index, {std::move(subject)}};
        return result;
    };
    const auto strip = [](CXCursor value) {
        while (clang_getCursorKind(value) == CXCursor_UnexposedExpr ||
               clang_getCursorKind(value) == CXCursor_ParenExpr) {
            const auto nested = children_of(value);
            if (nested.size() != 1)
                break;
            value = nested[0];
        }
        return value;
    };
    if (kind == CXCursor_BinaryOperator && (clang_getCursorBinaryOperatorKind(cursor) == CXBinaryOperator_EQ ||
                                            clang_getCursorBinaryOperatorKind(cursor) == CXBinaryOperator_NE)) {
        const auto children = children_of(cursor);
        if (children.size() == 2) {
            for (unsigned side = 0; side != 2; ++side) {
                if (clang_getCursorKind(strip(children[side])) != CXCursor_CXXNullPtrLiteralExpr)
                    continue;
                Expr subject = build_expression(children[1 - side], signature, locals, depth + 1);
                if (subject.type.representation.kind != source::RepresentationKind::Pointer)
                    continue;
                auto nullness = make_projection(std::move(subject), 0);
                if (clang_getCursorBinaryOperatorKind(cursor) == CXBinaryOperator_NE) {
                    Expr negated;
                    negated.type = nullness.type;
                    negated.location = nullness.location;
                    negated.node = Negation{{std::move(nullness)}};
                    return negated;
                }
                return nullness;
            }
        }
    }
    // A dereference reads the pointee place, at its own current version. The
    // place was formed before the expression was lowered, where the capability
    // obligation was owed: reaching here without one is impossible, which is
    // why no capability is re-checked at the read (RFC 0014 §17 step 6).
    if (kind == CXCursor_UnaryOperator && clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_Deref) {
        if (const auto access = resolve_access(cursor); access && access->receiver) {
            // A body tracks every leaf of its implicit object, so `*this` is read
            // whole as they assemble it (TRUST.md TCB-AGGREGATE-001). A clause
            // reads each leaf as the parameter it is, and has no such value.
            if (!signature.clause) {
                if (std::optional<Expr> whole =
                        aggregates::receiver_value(cursor, *access, locals, frame_of(signature))) {
                    return std::move(*whole);
                }
            }
            return unsupported_expression(cursor, "the implicit object is not one modeled value; a member function "
                                                  "reads and writes it member by member (SPEC.md CLASS-008)");
        }
        if (const auto pointee = tracked_place(cursor, locals, signature)) {
            return read_place(locals, *pointee, cursor);
        }
    }
    // A member of the implicit object, however it is written: `this->x`,
    // `(*this).x`, or `x` alone (SPEC.md CLASS-008).
    if (kind == CXCursor_MemberRefExpr && on_implicit_object(cursor)) {
        if (const std::optional<std::string> unmodeled = unmodeled_member(cursor)) {
            return unsupported_expression(cursor, *unmodeled + " (SPEC.md CLASS-010, CLASS-015)");
        }
    }
    if (kind == CXCursor_MemberRefExpr || kind == CXCursor_ArraySubscriptExpr) {
        if (const auto access = resolve_access(cursor); access && access->receiver) {
            return read_receiver(*access, cursor, signature, locals);
        }
    }
    if (kind == CXCursor_MemberRefExpr) {
        const auto field = clang_getCursorReferenced(cursor);
        const auto children = children_of(cursor);
        if (clang_getCursorKind(field) == CXCursor_FieldDecl && children.size() == 1) {
            // A bit-field holds only the values of its width, and C++ promotes
            // it by that width rather than by its declared type: `unsigned x :
            // 31` promotes to `int`, `unsigned y : 32` to `unsigned int`. Its
            // value is not modeled, so a read fails closed (SPEC.md ARITH-003).
            if (clang_getFieldDeclBitWidth(field) >= 0) {
                return unsupported_expression(cursor, "bit-field '" + take(clang_getCursorSpelling(field)) +
                                                          "' is not modeled: its values and its promotion follow its "
                                                          "width, not its declared type");
            }
            // A member of a tracked object is its own place, so it is read at
            // its own current version rather than projected out of a value of
            // the whole object: a later write to a sibling must not disturb it,
            // and a write to this member must (SPEC.md 12.10).
            if (const auto member = tracked_place(cursor, locals, signature)) {
                return read_place(locals, *member, cursor);
            }
            if (std::optional<Expr> whole = aggregates::member_value(cursor, locals, frame_of(signature))) {
                return std::move(*whole);
            }
            Expr subject = build_expression(children[0], signature, locals, depth + 1);
            const auto& components = subject.type.representation.components;
            const auto name = take(clang_getCursorSpelling(field));
            // Clang resolved this access, access control included, so a member
            // the representation marks inaccessible from outside its class --
            // one a member function reads of another object of its own class --
            // is read here as the member it is (SPEC.md CONTRACT-008).
            const bool member_of_own_class =
                signature.receiver.has_value() &&
                clang_equalCursors(enclosing_record(field), signature.receiver->record) != 0;
            for (std::size_t i = 0; i < components.size(); ++i)
                if (components[i].name == name && (components[i].accessible || member_of_own_class))
                    return make_projection(std::move(subject), static_cast<std::uint32_t>(i));
            // The object's type states why it has no components to select from --
            // a base subobject, a union, an incomplete type. Reporting that is
            // the difference between naming the obstacle and calling every such
            // object "not modeled" (AGENTS.md 35).
            if (const auto& rejection = subject.type.representation.rejection; !rejection.empty())
                return unsupported_expression(cursor,
                                              "'" + subject.type.spelling + "' is not decomposed: " + rejection);
        }
    }
    if (kind == CXCursor_ArraySubscriptExpr) {
        const auto children = children_of(cursor);
        if (children.size() == 2) {
            // An element of a tracked array is its own place, read at its own
            // current version, so a write to one element leaves the others
            // alone (SPEC.md 12.10).
            if (const auto element = tracked_place(cursor, locals, signature)) {
                return read_place(locals, *element, cursor);
            }
            if (std::optional<Expr> whole = aggregates::member_value(cursor, locals, frame_of(signature))) {
                return std::move(*whole);
            }
            Expr subject = build_expression(strip(children[0]), signature, locals, depth + 1);
            if (subject.type.representation.kind == source::RepresentationKind::Array) {
                bool constant = false;
                if (CXEvalResult evaluated = clang_Cursor_Evaluate(children[1])) {
                    const bool integral = clang_EvalResult_getKind(evaluated) == CXEval_Int;
                    const auto index = integral ? clang_EvalResult_getAsLongLong(evaluated) : -1;
                    clang_EvalResult_dispose(evaluated);
                    constant = integral;
                    if (index >= 0 && static_cast<std::size_t>(index) < subject.type.projections.size())
                        return make_projection(std::move(subject), static_cast<std::uint32_t>(index));
                }
                // An index Clang already folded to a constant is not symbolic.
                // One outside the extent selects no element of this array, and
                // observing it at a term would turn a decided out-of-bounds
                // access into an obligation that merely fails to prove.
                if (constant) {
                    return unsupported_expression(cursor,
                                                  "proof array index must be a constant within the resolved extent");
                }
                // A symbolic index observes the element at a term instead
                // (FOUNDATIONS.md 45). The extent comes from the resolved array
                // type rather than from whichever elements happen to have been
                // observed already, so it is known without any prior element
                // fact (ARCHITECTURE.md ARCH-ELEM-004).
                return element_observation(std::move(subject), children[1], signature, locals, depth, cursor);
            }
        }
    }
    if (kind == CXCursor_CallExpr) {
        if (const std::optional<SequenceCall> call = sequence_call(cursor)) {
            return sequence_expression(*call, cursor, signature, locals, depth);
        }
    }
    // An explicit `std::span<const T>(v)` is the same conversion an argument
    // performs implicitly (RFC 0020 §7).
    if (kind == CXCursor_CXXFunctionalCastExpr) {
        const auto children = children_of(cursor);
        const auto operand = std::ranges::find_if(
            children, [](CXCursor child) { return clang_isExpression(clang_getCursorKind(child)) != 0; });
        if (operand != children.end()) {
            if (const std::optional<SequenceCall> call = sequence_call(strip_parens(*operand));
                call && call->constructor && call->family == source::RepresentationKind::Span) {
                return sequence_expression(*call, strip_parens(*operand), signature, locals, depth);
            }
        }
    }
    if (kind == CXCursor_CallExpr) {
        const auto called = clang_getCursorReferenced(cursor);
        const auto children = children_of(cursor);
        if (clang_getCursorKind(called) == CXCursor_CXXMethod && clang_Cursor_getNumArguments(cursor) == 0 &&
            !children.empty()) {
            const auto member = children_of(children[0]);
            if (member.size() == 1) {
                Expr subject = build_expression(member[0], signature, locals, depth + 1);
                const auto family = subject.type.representation.kind;
                const auto name = take(clang_getCursorSpelling(called));
                if ((family == source::RepresentationKind::Optional ||
                     family == source::RepresentationKind::Expected) &&
                    name == "has_value")
                    return make_projection(std::move(subject), 0);
                if (family == source::RepresentationKind::Variant && name == "index")
                    return make_projection(std::move(subject), 0);
            }
        }
    }

    if (kind == CXCursor_ConditionalOperator) {
        const auto parts = children_of(cursor);
        if (parts.size() != 3)
            return unsupported_expression(cursor, "malformed conditional expression");
        Expr result;
        result.type = convert_type(clang_getCursorType(cursor));
        result.location = presumed_location(clang_getCursorLocation(cursor));
        Conditional choice;
        for (const auto& part : parts)
            choice.operands.push_back(build_expression(part, signature, locals, depth + 1));
        result.node = std::move(choice);
        return result;
    }

    // An explicit cast is modeled where it is the value-preserving scoped-enum
    // -> exact underlying-type cast, or a conversion between two modeled
    // integer types, which denotes exactly what the implicit conversion between
    // them would (SPEC.md ARITH-008). Clang resolves both types; every other
    // cast still fails closed.
    if (kind == CXCursor_CXXStaticCastExpr || kind == CXCursor_CStyleCastExpr ||
        kind == CXCursor_CXXFunctionalCastExpr) {
        const auto children = children_of(cursor);
        const auto operand = std::ranges::find_if(
            children, [](CXCursor child) { return clang_isExpression(clang_getCursorKind(child)) != 0; });
        if (operand != children.end()) {
            const Type destination = convert_type(clang_getCursorType(cursor));
            const Type source = convert_type(clang_getCursorType(*operand));
            if (kind == CXCursor_CXXStaticCastExpr && !source.representation.identity.empty() &&
                destination.representation.identity.empty() && destination.kind == TypeKind::Int &&
                destination.width == source.width && destination.is_signed == source.is_signed) {
                Expr expression = build_expression(*operand, signature, locals, depth + 1);
                expression.type = destination;
                return expression;
            }
            // Clang puts the conversion a cast to or from `bool` performs in an
            // implicit node beneath it, so a cast to `bool` reads a `bool`.
            if (destination.kind == TypeKind::Bool && source.kind == TypeKind::Bool) {
                return build_expression(*operand, signature, locals, depth + 1);
            }
            if (integral(destination) && integral(source)) {
                Expr expression = build_expression(*operand, signature, locals, depth + 1);
                if (same_modeled_value(destination, source)) {
                    // Clang often records a cast's conversion as an implicit
                    // one beneath it; it is still the conversion written here.
                    if (auto* conversion = std::get_if<Conversion>(&expression.node)) {
                        conversion->written = true;
                    }
                    expression.type = destination;
                    return expression;
                }
                return integral_conversion(std::move(expression), destination, cursor, true);
            }
            return unsupported_expression(cursor, "an explicit conversion from '" + source.spelling + "' to '" +
                                                      destination.spelling +
                                                      "' is not modeled: only a scoped enum cast to its exact "
                                                      "underlying type and a cast between integer types are");
        }
        return unsupported_expression(cursor, "an explicit conversion is not modeled");
    }

    // Nodes Clang inserts that carry no meaning of their own are traversed
    // through while they do not change the value. One that changes it is a
    // conversion: between two modeled integer types it is the integral
    // conversion Clang put there -- a promotion, a usual arithmetic conversion,
    // or the conversion of an initializer, an argument or a returned value --
    // and any other is refused (SPEC.md ARITH-008).
    if (kind == CXCursor_UnexposedExpr || kind == CXCursor_ParenExpr) {
        const std::vector<CXCursor> inner = children_of(cursor);
        if (inner.size() != 1) {
            return unsupported_expression(cursor, "unsupported implicit expression node");
        }
        const CXType outer = clang_getCanonicalType(clang_getCursorType(cursor));
        const CXType nested = clang_getCanonicalType(clang_getCursorType(inner[0]));
        const Type converted = convert_type(outer);
        const Type original = convert_type(nested);
        if (clang_equalTypes(outer, nested) == 0 && !same_modeled_value(converted, original)) {
            if (kind == CXCursor_UnexposedExpr && integral(converted) && integral(original)) {
                return integral_conversion(build_expression(inner[0], signature, locals, depth + 1), converted, cursor,
                                           false);
            }
            if (kind == CXCursor_UnexposedExpr && boolean_pair(original, converted)) {
                return boolean_conversion(build_expression(inner[0], signature, locals, depth + 1), converted, cursor);
            }
            // An unscoped enumeration converts implicitly to an integer type:
            // its value is a value of its underlying type, which is what Clang
            // converts (SPEC.md ARITH-008). A scoped one never does.
            if (kind == CXCursor_UnexposedExpr && integral(converted) && nested.kind == CXType_Enum &&
                clang_EnumDecl_isScoped(clang_getTypeDeclaration(nested)) == 0) {
                Expr value = build_expression(inner[0], signature, locals, depth + 1);
                if (std::holds_alternative<Unsupported>(value.node)) {
                    return value;
                }
                Type underlying = convert_type(clang_getEnumDeclIntegerType(clang_getTypeDeclaration(nested)));
                if (!integral(underlying)) {
                    return unsupported_expression(cursor, "the underlying type of '" +
                                                              take(clang_getTypeSpelling(nested)) + "' is not modeled");
                }
                value.type = underlying;
                if (same_modeled_value(converted, underlying)) {
                    value.type = converted;
                    return value;
                }
                return integral_conversion(std::move(value), converted, cursor, false);
            }
            return unsupported_expression(cursor, "implicit conversion from '" + take(clang_getTypeSpelling(nested)) +
                                                      "' to '" + take(clang_getTypeSpelling(outer)) +
                                                      "' is not modeled");
        }
        return build_expression(inner[0], signature, locals, depth + 1);
    }

    if (kind == CXCursor_DeclRefExpr) {
        const CXCursor referenced = clang_getCursorReferenced(cursor);
        if (clang_getCursorKind(referenced) == CXCursor_EnumConstantDecl) {
            Expr expression;
            expression.type = convert_type(clang_getCursorType(cursor));
            expression.location = presumed_location(clang_getCursorLocation(cursor));
            const Type underlying =
                convert_type(clang_getEnumDeclIntegerType(clang_getCursorSemanticParent(referenced)));
            expression.node = IntLiteral{enumerator_value(referenced, underlying.is_signed)};
            return expression;
        }
        // A view or an element reference designates storage formed at a
        // generation; read at another it may designate nothing (STDMODEL-015).
        if (const std::optional<std::size_t> binding = find_binding(locals, referenced)) {
            if (std::optional<std::string> stale = stale_borrow(locals, *binding)) {
                return unsupported_expression(cursor, std::move(*stale));
            }
        }
        if (const std::optional<std::size_t> local = find_local(locals, referenced)) {
            if (const std::optional<CaseBinder>& binder = locals[*local].binder) {
                Expr bound;
                bound.type = locals[*local].type;
                bound.location = presumed_location(clang_getCursorLocation(cursor));
                bound.node = *binder;
                return bound;
            }
            return read_place(locals, *local, cursor);
        }
        // An object this body tracks as one place per scalar leaf -- an aggregate
        // local, or a by-value aggregate parameter it writes -- is read whole as
        // the value its leaves hold here, not as the one it was declared or
        // passed with (TRUST.md TCB-AGGREGATE-001).
        if (std::optional<Expr> whole = aggregates::object_value(cursor, referenced, locals, frame_of(signature))) {
            return std::move(*whole);
        }
        if (const std::optional<std::uint32_t> position = signature.position_of(referenced)) {
            Expr expr;
            expr.type = convert_type(clang_getCursorType(cursor));
            expr.location = presumed_location(clang_getCursorLocation(cursor));
            expr.node = ParameterRef{*position, take(clang_getCursorSpelling(referenced))};
            return expr;
        }
        // In a specialization a non-type template parameter denotes the value it
        // was instantiated at. Clang has already substituted it, and reports the
        // reference with no referent declaration: what remains is a constant of
        // the parameter's type. Asking Clang to evaluate it reads back that
        // substitution; none is performed here (SPEC.md 42, TEMPLATE-001).
        //
        // A reference that does not evaluate is one Clang left dependent, so
        // this is not a resolved specialization. It falls through to the
        // refusal below rather than becoming a value nothing established.
        if (clang_getCursorKind(referenced) == CXCursor_NonTypeTemplateParameter ||
            clang_Cursor_isNull(referenced) != 0 || clang_getCursorKind(referenced) == CXCursor_NoDeclFound) {
            Expr literal = build_integer_literal(cursor);
            if (!std::holds_alternative<Unsupported>(literal.node)) {
                return literal;
            }
        }
        // A namespace-scope or static constant, `constexpr` or `const` and not
        // `volatile`, holds the value its constant initializer gives it for the
        // whole run: writing it is undefined behavior. That value is the one
        // Clang computes. One whose initializer Clang cannot compute is not a
        // constant here and is refused.
        if (clang_getCursorKind(referenced) == CXCursor_VarDecl &&
            clang_Cursor_hasVarDeclGlobalStorage(referenced) != 0) {
            const CXType declared = clang_getCursorType(referenced);
            if (clang_isConstQualifiedType(declared) != 0 && clang_isVolatileQualifiedType(declared) == 0) {
                Expr constant = build_integer_literal(cursor);
                if (!std::holds_alternative<Unsupported>(constant.node)) {
                    return constant;
                }
                return unsupported_expression(cursor, "'" + take(clang_getCursorSpelling(referenced)) +
                                                          "' is a constant whose value Clang cannot compute");
            }
        }
        // C++ puts a local in scope inside its own initializer, so scoping
        // alone does not rule out a read before the local holds a value.
        if (clang_getCursorKind(referenced) == CXCursor_VarDecl &&
            clang_Cursor_hasVarDeclGlobalStorage(referenced) == 0) {
            return unsupported_expression(cursor, "local '" + take(clang_getCursorSpelling(referenced)) +
                                                      "' is read where it holds no modeled value, such as in its own "
                                                      "initializer");
        }
        return unsupported_expression(cursor, "'" + take(clang_getCursorSpelling(referenced)) +
                                                  "' is not a parameter or local of the enclosing "
                                                  "declaration");
    }

    // A character literal is an integer literal of its character type; `char`
    // is signed or not as the target makes it, which Clang has decided.
    if (kind == CXCursor_IntegerLiteral || kind == CXCursor_CXXBoolLiteralExpr || kind == CXCursor_CharacterLiteral) {
        return build_integer_literal(cursor);
    }

    // `sizeof`, `alignof`, `noexcept` and a `requires` expression are constants
    // Clang computes for the target the unit is compiled for, and their operand
    // is never evaluated, so it owes nothing. One Clang cannot compute, the size
    // of a variable-length array, is refused.
    if (kind == CXCursor_UnaryExpr || kind == CXCursor_RequiresExpr) {
        Expr constant = build_integer_literal(cursor);
        if (std::holds_alternative<Unsupported>(constant.node)) {
            return unsupported_expression(cursor, "this 'sizeof', 'alignof', 'noexcept' or 'requires' expression "
                                                  "is not a constant Clang computes");
        }
        return constant;
    }

    // Unary `+` and `-` on an integer operand C++ has already promoted: the
    // promotion is the operand's own conversion. `+x` is that value. `-x` is
    // its negation, which for a signed type owes representability like a
    // subtraction from zero (SPEC.md ARITH-006). The negation of an integer
    // literal is a literal: C++ has no negative literals, so `-1` is written
    // this way, and negating a literal's nonnegative value never overflows.
    if (kind == CXCursor_UnaryOperator && (clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_Minus ||
                                           clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_Plus)) {
        const auto operands = children_of(cursor);
        const Type type = convert_type(clang_getCursorType(cursor));
        const bool minus = clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_Minus;
        if (operands.size() != 1 || !integral(type)) {
            return unsupported_expression(cursor, std::string("operator '") + (minus ? "-" : "+") + "' on '" +
                                                      type.spelling + "' is not modeled");
        }
        Expr operand = build_expression(operands[0], signature, locals, depth + 1);
        if (!same_modeled_value(type, operand.type)) {
            return unsupported_expression(cursor, std::string("operator '") + (minus ? "-" : "+") +
                                                      "' of an operand of another type is not modeled");
        }
        if (!minus) {
            operand.type = type;
            return operand;
        }
        if (const auto* literal = std::get_if<IntLiteral>(&operand.node)) {
            if (!type.is_signed) {
                // Reduction modulo 2^width of the negated bits, kept in the
                // 64-bit carrier the way an unsigned literal's bits are.
                const auto bits = static_cast<std::uint64_t>(literal->value);
                const std::uint64_t mask = type.width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << type.width) - 1u;
                operand.node = IntLiteral{static_cast<std::int64_t>((std::uint64_t{0} - bits) & mask)};
                operand.type = type;
                operand.location = presumed_location(clang_getCursorLocation(cursor));
                return operand;
            }
            // A literal's value is never negative, but a template argument
            // Clang substituted may be the least value, whose negation is not
            // a value: that one keeps its negation and the obligation it owes.
            const std::int64_t greatest =
                type.width >= 64 ? std::numeric_limits<std::int64_t>::max() : (std::int64_t{1} << (type.width - 1)) - 1;
            if (literal->value >= -greatest && literal->value <= greatest) {
                operand.node = IntLiteral{-literal->value};
                operand.type = type;
                operand.location = presumed_location(clang_getCursorLocation(cursor));
                return operand;
            }
        }
        Expr negated;
        negated.type = type;
        negated.location = presumed_location(clang_getCursorLocation(cursor));
        negated.node = Minus{{std::move(operand)}};
        return negated;
    }

    if (kind == CXCursor_CallExpr) {
        const CXCursor referenced = clang_getCursorReferenced(cursor);
        if (clang_Cursor_isNull(referenced) != 0) {
            if (calls_through_member_pointer(cursor)) {
                return unsupported_expression(cursor,
                                              "a call through a pointer to member function is not modeled: which "
                                              "function it runs is a value, not a declaration (SPEC.md CLASS-015)");
            }
            return unsupported_expression(cursor, "call does not resolve to an ordinary function");
        }
        // A copy or a move C++ defines memberwise is the value it copies. Any
        // other construction is refused by what it would run (TRUST.md
        // TCB-AGGREGATE-001).
        if (clang_getCursorKind(referenced) == CXCursor_Constructor) {
            const std::expected<CXCursor, std::string> operand = aggregates::copied_operand(cursor);
            if (!operand) {
                return unsupported_expression(cursor, operand.error());
            }
            return build_expression(*operand, signature, locals, depth + 1, sequenced_call);
        }
        const bool member = clang_getCursorKind(referenced) == CXCursor_CXXMethod;
        if (clang_getCursorKind(referenced) != CXCursor_FunctionDecl && !member) {
            return unsupported_expression(cursor, "call does not resolve to an ordinary function");
        }
        // A member function called on an object is the verified callable its
        // declaration is, with the object's leaves as the arguments of its
        // implicit object (SPEC.md CLASS-011). A static one has no object.
        std::optional<Receiver> callee_receiver;
        std::optional<CallObject> object;
        if (member && clang_CXXMethod_isStatic(referenced) == 0) {
            if (clang_CXXMethod_isVirtual(referenced) != 0) {
                return unsupported_expression(
                    cursor, std::string(clang_Cursor_isDynamicCall(cursor) != 0 ? "a virtual call dispatches on"
                                                                                : "a call to a virtual function names "
                                                                                  "one whose overrides depend on") +
                                " the object's dynamic type, and override substitutability is not checked by this "
                                "implementation (SPEC.md CLASS-006, CLASS-014)");
            }
            auto receiver = receiver_of(referenced, nullptr);
            if (!receiver) {
                return unsupported_expression(
                    cursor, "'" + qualified_name_of(referenced) +
                                "' is not a member function this implementation verifies: " + receiver.error());
            }
            auto resolved = call_object(cursor, referenced);
            if (!resolved) {
                return unsupported_expression(cursor, resolved.error());
            }
            callee_receiver = std::move(*receiver);
            object = std::move(*resolved);
        }

        if (callee_receiver.has_value() && callee_receiver->writes() && !sequenced_call) {
            return unsupported_expression(cursor,
                                          "a mutating call requires a sequenced statement, initializer or assignment");
        }
        for (int index = 0; index < clang_Cursor_getNumArguments(referenced); ++index) {
            if (source::may_write(passing_of(
                    clang_getCursorType(clang_Cursor_getArgument(referenced, static_cast<unsigned>(index))))) &&
                !sequenced_call)
                return unsupported_expression(
                    cursor, "a mutating call requires a sequenced statement, initializer or assignment");
        }
        // A callee whose unsafe code may write what it is handed writes, as far
        // as this body can tell, every reference, pointer and view it is handed
        // and its object (TRUST.md TCB-UNSAFE-004). That write is followed only
        // where the call is a statement, an initializer or an assignment of its
        // own; anywhere else it would go unseen.
        if (!sequenced_call && writes_unsafely(signature.unsafe_effects, referenced)) {
            return unsupported_expression(cursor, "'" + qualified_name_of(referenced) +
                                                      "' has unsafe code that may write what it is handed, so a call "
                                                      "to it requires a statement, initializer or assignment of its "
                                                      "own (TRUST.md TCB-UNSAFE-004)");
        }
        Call call;
        call.callee_usr = take(clang_getCursorUSR(referenced));
        call.callee_name = qualified_name_of(referenced);
        // A validation expression reaches the analysis text as a call of its
        // refinement's predicate probe, a declaration only the projector can
        // name (SPEC.md RUNTIMECHECK-018, WORD-013).
        if (const std::string spelled = take(clang_getCursorSpelling(referenced));
            !member && spelled.starts_with("__cppl_refinement_")) {
            call.validation = spelled;
            call.callee_name = "validate";
        }

        // The implicit object's arguments come first, in the order of the
        // callee's leaves, each the object's own storage at that path.
        if (callee_receiver.has_value() && object.has_value()) {
            for (const ReceiverLeaf& leaf : callee_receiver->leaves) {
                std::vector<PlaceStep> path = object->path;
                path.insert(path.end(), leaf.path.begin(), leaf.path.end());
                Expr argument = receiver_argument(*object, path, leaf, signature, locals, cursor);
                if (std::holds_alternative<Unsupported>(argument.node)) {
                    return argument;
                }
                call.arguments.push_back(std::move(argument));
            }
        }

        const int argument_count = clang_Cursor_getNumArguments(cursor);
        if (argument_count < 0) {
            return unsupported_expression(cursor, "call arguments could not be resolved");
        }
        for (int index = 0; index < argument_count; ++index) {
            const CXCursor argument = clang_Cursor_getArgument(cursor, static_cast<unsigned>(index));
            // An argument the call does not write is its parameter's default,
            // evaluated here as though written here (SPEC.md R.16).
            if (is_default_argument(argument)) {
                call.arguments.push_back(lower_default_argument(cursor, referenced, static_cast<unsigned>(index),
                                                                argument, signature, depth));
                continue;
            }
            // A container's data pointer is admitted here and nowhere else: as
            // an argument whose parameter a callee's capability describes, over
            // the container's length (RFC 0020 §7, STDMODEL-017).
            if (const std::optional<SequenceCall> data = sequence_call(strip_parens(argument));
                data && !data->constructor && data->name == "data" && data->arguments.empty() &&
                source::is_sequence(data->family)) {
                Expr subject = build_expression(data->object, signature, locals, depth + 1);
                if (std::holds_alternative<Unsupported>(subject.node)) {
                    call.arguments.push_back(std::move(subject));
                    continue;
                }
                const Type container = subject.type;
                call.arguments.push_back(library_call({data->family, source::LibraryOperation::DataOf}, container,
                                                      library_name(data->family, "data"), {std::move(subject)},
                                                      convert_type(clang_getCursorType(argument)), argument));
                continue;
            }
            call.arguments.push_back(build_expression(argument, signature, locals, depth + 1));
        }

        Expr expr;
        expr.type = convert_type(clang_getCursorType(cursor));
        expr.location = presumed_location(clang_getCursorLocation(cursor));
        expr.node = std::move(call);
        return expr;
    }

    if (kind == CXCursor_UnaryOperator && clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_LNot) {
        const auto operands = children_of(cursor);
        if (operands.size() != 1)
            return unsupported_expression(cursor, "malformed negation");
        Expr expr;
        expr.type = convert_type(clang_getCursorType(cursor));
        expr.location = presumed_location(clang_getCursorLocation(cursor));
        expr.node = Negation{{build_expression(operands[0], signature, locals, depth + 1)}};
        return expr;
    }

    if (kind == CXCursor_BinaryOperator) {
        const enum CXBinaryOperatorKind op = clang_getCursorBinaryOperatorKind(cursor);
        BinaryOp mapped = BinaryOp::Unsupported;
        if (op == CXBinaryOperator_Add) {
            mapped = BinaryOp::Add;
        } else if (op == CXBinaryOperator_Sub) {
            mapped = BinaryOp::Sub;
        } else if (op == CXBinaryOperator_Mul) {
            mapped = BinaryOp::Mul;
        } else if (op == CXBinaryOperator_Div) {
            mapped = BinaryOp::Div;
        } else if (op == CXBinaryOperator_Rem) {
            mapped = BinaryOp::Rem;
        } else if (op == CXBinaryOperator_EQ) {
            mapped = BinaryOp::Equal;
        } else if (op == CXBinaryOperator_NE) {
            mapped = BinaryOp::NotEqual;
        } else if (op == CXBinaryOperator_LT) {
            mapped = BinaryOp::Less;
        } else if (op == CXBinaryOperator_LE) {
            mapped = BinaryOp::LessEqual;
        } else if (op == CXBinaryOperator_GT) {
            mapped = BinaryOp::Greater;
        } else if (op == CXBinaryOperator_GE) {
            mapped = BinaryOp::GreaterEqual;
        } else if (op == CXBinaryOperator_LAnd) {
            mapped = BinaryOp::And;
        } else if (op == CXBinaryOperator_LOr) {
            mapped = BinaryOp::Or;
        }
        // As a statement, `a, b` is two statements. Inside an expression, the
        // effects and definedness of `a` would have to be modeled there.
        if (op == CXBinaryOperator_Comma) {
            return unsupported_expression(cursor, "the comma operator inside an expression is not modeled; write "
                                                  "its operands as statements of their own");
        }
        if (mapped == BinaryOp::Unsupported) {
            return unsupported_expression(cursor, "operator '" + take(clang_getBinaryOperatorKindSpelling(op)) +
                                                      "' is not modeled");
        }

        const std::vector<CXCursor> operands = children_of(cursor);
        if (operands.size() != 2) {
            return unsupported_expression(cursor, "binary operator does not have two operands");
        }

        Binary binary;
        binary.op = mapped;
        binary.operands.push_back(build_expression(operands[0], signature, locals, depth + 1));
        binary.operands.push_back(build_expression(operands[1], signature, locals, depth + 1));

        Expr expr;
        expr.type = convert_type(clang_getCursorType(cursor));
        expr.location = presumed_location(clang_getCursorLocation(cursor));
        expr.node = std::move(binary);
        return expr;
    }

    return unsupported_expression(cursor, unmodeled_expression(cursor, kind));
}

} // namespace cppl::clangbridge::detail
