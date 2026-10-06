#include "conversions.hpp"

#include "access.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "places.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// The conversions C++ performs between the types this implementation models,
// and whether a value of a type carries a refinement (SPEC.md ARITH-008,
// 17.2.1).
namespace cppl::clangbridge::detail {

using bridge::presumed_location;

// Whether two Clang types denote the same modeled value. Qualifiers are not
// part of a value, so a read of a `const` local is the value it holds; two
// spellings that Clang laid out identically are the same machine integer. A
// type C++L does not model is never "the same" as anything.
// Whether a value of `type` carries a refinement anywhere in it: its own, or
// one of a member or an element it is made of (SPEC.md 17.2.1). Past the depth
// places are tracked to, the answer is yes, which only costs an obligation.
bool carries_refinement(const Type& type, unsigned depth) {
    if (!type.refinements.empty() || depth > kMaxPlaceDepth) {
        return true;
    }
    return std::ranges::any_of(type.projections,
                               [depth](const Type& component) { return carries_refinement(component, depth + 1); });
}

bool same_modeled_value(const Type& outer, const Type& inner) {
    return outer.kind != TypeKind::Unsupported && outer.kind == inner.kind && outer.width == inner.width &&
           outer.is_signed == inner.is_signed && outer.representation == inner.representation;
}

// Whether a type is a built-in integer type this implementation models, as
// the operand or the result of an integral conversion. A scoped enum carries
// its underlying type but is not one: converting it is a cast of its own, and
// `bool` converts by truth, not by reduction (SPEC.md ARITH-008).
bool integral(const Type& type) {
    return type.kind == TypeKind::Int && type.representation.identity.empty() && type.width >= 1 && type.width <= 64;
}

// The conversion of `operand` to `type`, where Clang converts one modeled
// integer type to another. Nothing is decided here about which conversion C++
// performs: `type` is the one Clang recorded.
Expr integral_conversion(Expr operand, Type type, CXCursor at, bool written) {
    Expr converted;
    converted.type = std::move(type);
    converted.location = presumed_location(clang_getCursorLocation(at));
    converted.node = Conversion{{std::move(operand)}, written};
    return converted;
}

// The conversion of `operand` to `type` where exactly one of the two is `bool`
// and the other a modeled integer type (SPEC.md ARITH-008): an integer converts
// to `bool` as whether it is nonzero, and `bool` to an integer type as 1 when
// true and 0 when false, which is what C++ defines each to be. The second is the
// core's conversion of its one-bit value; the first is never stated with it.
Expr boolean_conversion(Expr operand, Type type, CXCursor at) {
    if (type.kind == TypeKind::Bool) {
        Expr zero;
        zero.type = operand.type;
        zero.location = operand.location;
        zero.node = IntLiteral{0};
        Expr nonzero;
        nonzero.type = std::move(type);
        nonzero.location = presumed_location(clang_getCursorLocation(at));
        nonzero.node = Binary{BinaryOp::NotEqual, {std::move(operand), std::move(zero)}};
        return nonzero;
    }
    return integral_conversion(std::move(operand), std::move(type), at, false);
}

// Whether `from` and `to` are a modeled integer type and `bool`, in either order.
bool boolean_pair(const Type& from, const Type& to) {
    return (from.kind == TypeKind::Bool && integral(to)) || (integral(from) && to.kind == TypeKind::Bool);
}

// Whether C++ may perform arithmetic on this type only after an integral
// promotion: to `int` where `int` holds every value, otherwise to `unsigned
// int`. libclang does not expose the type a compound assignment computes in, so
// this errs toward yes: every type but those of `int`'s rank or above counts as
// promoted, and an update of such a local, which converts the promoted result
// back, is refused rather than given a computation type derived here.
bool promoted_before_arithmetic(CXType type) {
    const auto canonical = clang_getCanonicalType(type);
    if (canonical.kind == CXType_LValueReference || canonical.kind == CXType_RValueReference)
        type = reference_value_type(type);
    switch (clang_getCanonicalType(type).kind) {
        case CXType_Int:
        case CXType_UInt:
        case CXType_Long:
        case CXType_ULong:
        case CXType_LongLong:
        case CXType_ULongLong:
            return false;
        default:
            return true;
    }
}

} // namespace cppl::clangbridge::detail
