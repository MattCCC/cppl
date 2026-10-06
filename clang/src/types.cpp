#include "types.hpp"

#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "places.hpp"
#include "refinements.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// How the Clang bridge models a resolved type: its representation, the
// standard sequences it admits and why it refuses the rest, enumerators at the
// underlying type's width, whether destroying an object runs code of the
// program, and how a diagnostic names a declaration, a location and an
// expression this implementation does not model.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::record_fields;
using bridge::record_has_base;
using bridge::take;

namespace {

// Whether a record declares a destructor it neither defaults nor deletes: user
// code that runs where an object's lifetime ends. An instantiation may not
// expose its members as cursors, so the template it was instantiated from is
// asked too.
bool has_user_provided_destructor(CXCursor definition) {
    const auto declares = [](CXCursor record) {
        bool found = false;
        clang_visitChildren(
            record,
            [](CXCursor child, CXCursor, CXClientData data) {
                if (clang_getCursorKind(child) == CXCursor_Destructor && clang_CXXMethod_isDefaulted(child) == 0 &&
                    clang_CXXMethod_isDeleted(child) == 0) {
                    *static_cast<bool*>(data) = true;
                    return CXChildVisit_Break;
                }
                return CXChildVisit_Continue;
            },
            &found);
        return found;
    };
    if (clang_Cursor_isNull(definition) != 0) {
        return false;
    }
    const CXCursor primary = clang_getSpecializedCursorTemplate(definition);
    return declares(definition) || (clang_Cursor_isNull(primary) == 0 && declares(primary));
}

// Whether destroying an object of `record` runs code of the program's: a
// user-provided destructor of the class, of a base, or of a member's class, at
// any depth.
bool destruction_runs_user_code(CXCursor record, unsigned depth = 0) {
    const CXCursor definition = clang_getCursorDefinition(record);
    if (clang_Cursor_isNull(definition) != 0 || depth > kMaxExpressionDepth) {
        return true;
    }
    if (has_user_provided_destructor(definition)) {
        return true;
    }
    std::vector<CXCursor> parts;
    clang_visitChildren(
        definition,
        [](CXCursor child, CXCursor, CXClientData data) {
            const CXCursorKind kind = clang_getCursorKind(child);
            if (kind == CXCursor_FieldDecl || kind == CXCursor_CXXBaseSpecifier) {
                static_cast<std::vector<CXCursor>*>(data)->push_back(child);
            }
            return CXChildVisit_Continue;
        },
        &parts);
    return std::ranges::any_of(parts, [&](CXCursor part) {
        CXType type = clang_getCanonicalType(clang_getCursorType(part));
        while (type.kind == CXType_ConstantArray) {
            type = clang_getCanonicalType(clang_getArrayElementType(type));
        }
        return type.kind == CXType_Record && destruction_runs_user_code(clang_getTypeDeclaration(type), depth + 1);
    });
}

// The width in bits of the target's `std::size_t`, which is every modeled
// sequence's `size_type`, or 0 when the target does not report one.
//
// Clang reports the target's pointer width, and `size_t` has that width on every
// target this implementation compiles for. That correspondence is not assumed
// where it matters: each length observation checks that the type Clang gave
// the `size()` call is exactly the modeled one, and refuses it otherwise.
unsigned size_width(CXCursor anywhere) {
    CXTargetInfo target = clang_getTranslationUnitTargetInfo(clang_Cursor_getTranslationUnit(anywhere));
    if (target == nullptr) {
        return 0;
    }
    const int width = clang_TargetInfo_getPointerWidth(target);
    clang_TargetInfo_dispose(target);
    return width == 32 || width == 64 ? static_cast<unsigned>(width) : 0U;
}

// The modeled length of a sequence: the target's `std::size_t`.
Type length_type(CXCursor anywhere) {
    Type length;
    const unsigned width = size_width(anywhere);
    if (width == 0U) {
        return length;
    }
    length.kind = TypeKind::Int;
    length.width = static_cast<std::uint16_t>(width);
    length.is_signed = false;
    length.spelling = "std::size_t";
    return length;
}

// Whether an element type is one a modeled sequence may hold: a built-in
// integer or `bool`, whatever cv-qualification the view adds (RFC 0020 §1).
// Anything else would give an element place a value no version could state.
bool modeled_element(CXType element) {
    switch (clang_getCanonicalType(element).kind) {
        case CXType_Bool:
        case CXType_Char_S:
        case CXType_SChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long:
        case CXType_LongLong:
        case CXType_Char_U:
        case CXType_UChar:
        case CXType_UShort:
        case CXType_UInt:
        case CXType_ULong:
        case CXType_ULongLong:
            return clang_isVolatileQualifiedType(clang_getCanonicalType(element)) == 0;
        default:
            return false;
    }
}

// Why a specialization of a modeled sequence is not one this implementation
// models, if it is not (RFC 0020 §1). Each condition is what the library
// summaries are stated for: a standard allocator, character traits of `char`,
// a dynamic extent, and scalar elements. `std::vector<bool>` is excluded by
// name: its `operator[]` returns a proxy object, not an element.
std::optional<std::string> sequence_refusal(CXType canonical, CXCursor declaration, source::RepresentationKind kind) {
    using K = source::RepresentationKind;
    if (clang_Type_getNumTemplateArguments(canonical) < 1) {
        return "its template arguments are not resolved";
    }
    const CXType element = clang_Type_getTemplateArgumentAsType(canonical, 0);
    if (kind == K::Vector) {
        if (clang_Type_getNumTemplateArguments(canonical) != 2 ||
            !is_standard_template(clang_Type_getTemplateArgumentAsType(canonical, 1), "allocator")) {
            return "a vector with an allocator other than std::allocator is not modeled";
        }
        const CXType allocated = clang_Type_getTemplateArgumentAsType(
            clang_getCanonicalType(clang_Type_getTemplateArgumentAsType(canonical, 1)), 0);
        if (clang_equalTypes(clang_getCanonicalType(allocated), clang_getCanonicalType(element)) == 0) {
            return "a vector whose allocator allocates another type is not modeled";
        }
        if (clang_getCanonicalType(element).kind == CXType_Bool) {
            return "std::vector<bool> is not modeled: its operator[] yields a proxy object rather than an element";
        }
    }
    if (kind == K::String) {
        const CXType character = clang_getCanonicalType(element);
        if (clang_Type_getNumTemplateArguments(canonical) != 3 ||
            (character.kind != CXType_Char_S && character.kind != CXType_Char_U) ||
            !is_standard_template(clang_Type_getTemplateArgumentAsType(canonical, 1), "char_traits") ||
            !is_standard_template(clang_Type_getTemplateArgumentAsType(canonical, 2), "allocator")) {
            return "only std::string, std::basic_string<char> with the standard traits and allocator, is modeled";
        }
    }
    if (kind == K::Span) {
        // `std::dynamic_extent` is the largest value of `std::size_t`. A span
        // of static extent states its length in its type; it is not modeled.
        const unsigned width = size_width(declaration);
        const unsigned long long dynamic =
            width >= 64U ? std::numeric_limits<unsigned long long>::max() : (1ULL << width) - 1ULL;
        if (width == 0U || clang_Type_getNumTemplateArguments(canonical) != 2 ||
            clang_Cursor_getTemplateArgumentKind(declaration, 1) != CXTemplateArgumentKind_Integral ||
            clang_Cursor_getTemplateArgumentUnsignedValue(declaration, 1) != dynamic) {
            return "only a span of dynamic extent is modeled";
        }
    }
    if (!modeled_element(element)) {
        return "its element type '" + take(clang_getTypeSpelling(element)) +
               "' is not modeled: a modeled sequence holds built-in integers or bool";
    }
    return std::nullopt;
}

} // namespace

// Where a cursor stands, as a diagnostic names it.
std::string describe_location(CXCursor at) {
    const source::SourceLocation where = presumed_location(clang_getCursorLocation(at));
    return where.file + ":" + std::to_string(where.line);
}

// Whether every temporary a full expression creates is destroyed without
// running code of the program's, so that the cleanup Clang wraps the statement
// in has no effect to model: no expression in it is of a class type whose
// destruction runs a user-provided destructor (SPEC.md STDMODEL-023).
bool temporaries_destroy_silently(CXCursor statement) {
    bool silent = true;
    clang_visitChildren(
        statement,
        [](CXCursor cursor, CXCursor, CXClientData data) {
            if (clang_isExpression(clang_getCursorKind(cursor)) == 0) {
                return CXChildVisit_Recurse;
            }
            const CXType type = clang_getCanonicalType(clang_getCursorType(cursor));
            if (type.kind == CXType_Record && destruction_runs_user_code(clang_getTypeDeclaration(type))) {
                *static_cast<bool*>(data) = false;
                return CXChildVisit_Break;
            }
            return CXChildVisit_Recurse;
        },
        &silent);
    return silent;
}

source::RepresentationKind library_kind(CXCursor declaration) {
    using K = source::RepresentationKind;
    CXCursor primary = clang_getSpecializedCursorTemplate(declaration);
    if (clang_Cursor_isNull(primary))
        return K::Record;
    primary = clang_getCanonicalCursor(primary);
    CXCursor parent = clang_getCursorSemanticParent(primary);
    while (clang_getCursorKind(parent) == CXCursor_Namespace && clang_Cursor_isInlineNamespace(parent))
        parent = clang_getCursorSemanticParent(parent);
    if (clang_getCursorKind(parent) != CXCursor_Namespace || take(clang_getCursorSpelling(parent)) != "std" ||
        clang_getCursorKind(clang_getCursorSemanticParent(parent)) != CXCursor_TranslationUnit)
        return K::Record;
    // This is declaration identity in the canonical standard namespace, not a
    // spelling of a source type. Alias expansion and substitution precede it.
    const std::string name = take(clang_getCursorSpelling(primary));
    if (name == "variant")
        return K::Variant;
    if (name == "optional")
        return K::Optional;
    if (name == "expected")
        return K::Expected;
    if (name == "pair")
        return K::Pair;
    if (name == "tuple")
        return K::Tuple;
    if (name == "array")
        return K::StdArray;
    // The standard sequences verified code may use (RFC 0020 §1). Which
    // specializations of them are modeled is decided with their arguments, in
    // `sequence_refusal`.
    if (name == "vector")
        return K::Vector;
    if (name == "basic_string")
        return K::String;
    if (name == "span")
        return K::Span;
    return K::Record;
}

// Whether `type`, canonical, is a specialization of the standard class template
// `name` (inline namespaces are transparent, as in `library_kind`).
bool is_standard_template(CXType type, std::string_view name) {
    const CXCursor declaration = clang_getTypeDeclaration(clang_getCanonicalType(type));
    CXCursor primary = clang_getSpecializedCursorTemplate(declaration);
    if (clang_Cursor_isNull(primary) != 0) {
        return false;
    }
    primary = clang_getCanonicalCursor(primary);
    CXCursor parent = clang_getCursorSemanticParent(primary);
    while (clang_getCursorKind(parent) == CXCursor_Namespace && clang_Cursor_isInlineNamespace(parent) != 0) {
        parent = clang_getCursorSemanticParent(parent);
    }
    return clang_getCursorKind(parent) == CXCursor_Namespace && take(clang_getCursorSpelling(parent)) == "std" &&
           clang_getCursorKind(clang_getCursorSemanticParent(parent)) == CXCursor_TranslationUnit &&
           take(clang_getCursorSpelling(primary)) == name;
}

// The same 64-bit pattern an integer literal of the underlying type carries.
// libclang's signed accessor sign-extends from the enumeration's own width, so an
// unsigned enumerator with its top bit set would otherwise arrive negative.
std::int64_t enumerator_value(CXCursor enumerator, bool underlying_is_signed) {
    if (underlying_is_signed)
        return clang_getEnumConstantDeclValue(enumerator);
    return static_cast<std::int64_t>(clang_getEnumConstantDeclUnsignedValue(enumerator));
}

// The refinements a record's members name, so a refined member's predicate
// reaches the member's own modeled type (SPEC.md 17.6).
//
// Clang canonicalizes a member's `Positive` to `int` exactly as it does a
// local's, so without this a declared refined member would be modeled as its
// base type and its construction would owe nothing. Passing the known
// refinements down is what lets the aggregate write path generate the member's
// obligation from the member's declared type, at the one site every write
// already uses.
Type convert_type(CXType type, unsigned depth, ReferenceModel references,
                  const std::vector<Selection::Refinement>* known) {
    CXType canonical = clang_getCanonicalType(type);
    if (canonical.kind == CXType_LValueReference || canonical.kind == CXType_RValueReference) {
        if (references != ReferenceModel::Referent) {
            Type reference;
            reference.spelling = take(clang_getTypeSpelling(canonical));
            return reference;
        }
        canonical = clang_getCanonicalType(clang_getPointeeType(canonical));
    }

    Type converted;
    converted.spelling = take(clang_getTypeSpelling(canonical));
    if (depth > 32)
        return converted;

    // A volatile glvalue is read for its effect, not for a value that is a
    // function of anything C++L models, so it is not a modeled type at all
    // (AGENTS.md 11). `const` is not such a qualifier: it constrains writes,
    // and the value read is the ordinary one.
    if (clang_isVolatileQualifiedType(canonical) != 0) {
        return converted;
    }

    // Layout is asked only of built-in integer types, which always have one.
    long long size = 0;
    switch (canonical.kind) {
        case CXType_Void:
            converted.kind = TypeKind::Void;
            break;
        case CXType_Pointer: {
            converted.kind = TypeKind::Value;
            converted.representation.identity = "pointer:" + converted.spelling;
            converted.representation.name = converted.spelling;
            converted.representation.kind = source::RepresentationKind::Pointer;
            Type state;
            state.kind = TypeKind::Bool;
            state.spelling = "bool";
            converted.projections.push_back(state);
            break;
        }
        case CXType_ConstantArray:
        case CXType_Record: {
            using K = source::RepresentationKind;
            const bool array = canonical.kind == CXType_ConstantArray;
            const CXCursor declaration = clang_getTypeDeclaration(canonical);
            const CXCursor definition = clang_getCursorDefinition(declaration);
            converted.kind = TypeKind::Value;
            auto& model = converted.representation;
            model.name = converted.spelling;
            model.identity = array ? "array:" + converted.spelling : take(clang_getCursorUSR(declaration));
            model.kind = array ? K::Array : library_kind(declaration);
            if (model.identity.empty()) {
                converted.kind = TypeKind::Unsupported;
                break;
            }
            // A modeled sequence is an abstract value with one observation, its
            // length (RFC 0020 §2). Its data members are the library's private
            // layout and are never read (TRUST.md TCB-LIB-002), and it has no
            // structural state model, so `cases` and `decompose` refuse it.
            if (source::is_sequence(model.kind)) {
                if (auto refusal = sequence_refusal(canonical, declaration, model.kind)) {
                    model.rejection = std::move(*refusal);
                    break;
                }
                Type length = length_type(declaration);
                if (length.kind != TypeKind::Int) {
                    model.rejection = "the target does not report the width of std::size_t";
                    break;
                }
                converted.projections.push_back(std::move(length));
                break;
            }
            if (!array && model.kind == K::Record && clang_Cursor_isNull(definition)) {
                model.rejection = "proof decomposition unavailable for incomplete type";
                break;
            }
            const auto component = [&](CXType child, std::string name, CXCursor origin, bool accessible = true) {
                Type resolved = convert_type(child, depth + 1, ReferenceModel::Opaque, known);
                if (resolved.kind == TypeKind::Unsupported) {
                    model.rejection = "component '" + name + "' has an unmodeled type '" + resolved.spelling + "'";
                    return;
                }
                // A member's or element's declared refinement belongs to that
                // storage's type, so every crossing into it owes the predicate
                // (SPEC.md 17.6). An array element's refinement is the element
                // type's own, which is why the origin need not be a field.
                if (known != nullptr) {
                    auto member_refinements = refinements_of(origin, child, *known);
                    if (!member_refinements) {
                        model.rejection = "component '" + name + "' has " + member_refinements.error().message;
                        return;
                    }
                    resolved.refinements = std::move(*member_refinements);
                }
                converted.projections.push_back(std::move(resolved));
                model.components.push_back(
                    {std::move(name), presumed_location(clang_getCursorLocation(origin)), accessible});
            };
            if (array || model.kind == K::StdArray) {
                long long count = array ? clang_getArraySize(canonical) : -1;
                // Take the element type from the written array type, not the
                // canonical one: canonicalizing discards the alias a refinement
                // is named by, and the element's predicate would be lost with
                // it (SPEC.md 17.3).
                CXType element =
                    array ? clang_getArrayElementType(type) : clang_Type_getTemplateArgumentAsType(canonical, 0);
                if (array && element.kind == CXType_Invalid)
                    element = clang_getArrayElementType(canonical);
                if (!array && clang_Cursor_getTemplateArgumentKind(declaration, 1) == CXTemplateArgumentKind_Integral)
                    count = clang_Cursor_getTemplateArgumentValue(declaration, 1);
                if (count < 0 || count > 256) {
                    model.rejection = "array extent is unavailable or exceeds the proof resource limit";
                    break;
                }
                // A refinement written as `std::array`'s element type is the
                // base type in the specialization, and no content invariant is
                // modeled for an array: reading one as the base type would
                // accept writes the declaration says it refuses, so it is
                // refused instead (SPEC.md STDMODEL-020).
                if (!array && known != nullptr) {
                    const CXType written = written_element_type(type);
                    auto stated =
                        written.kind == CXType_Invalid
                            ? std::expected<std::vector<Refinement>, RefinementFailure>{std::unexpected(
                                  RefinementFailure{Category::UnsupportedSemantics,
                                                    "an element type that could not be read from how the type is "
                                                    "written"})}
                            : refinements_of(clang_getNullCursor(), written, *known);
                    if (!stated || !stated->empty()) {
                        model.rejection = stated ? "its element type is written as the refinement '" +
                                                       stated->front().name +
                                                       "', which std::array does not state; a built-in array of "
                                                       "that refinement has refined elements (SPEC.md STDMODEL-020)"
                                                 : "it has " + stated.error().message;
                        break;
                    }
                }
                for (long long i = 0; i < count; ++i)
                    component(element, std::to_string(i), declaration);
            } else if (model.kind != K::Record) {
                const int count = clang_Type_getNumTemplateArguments(canonical);
                if (count < 0 || count > 64) {
                    model.rejection = "template arguments are unresolved or exceed the proof resource limit";
                    break;
                }
                if (model.kind == K::Variant || model.kind == K::Optional || model.kind == K::Expected) {
                    Type tag;
                    tag.kind = model.kind == K::Variant ? TypeKind::Int : TypeKind::Bool;
                    tag.width = 64;
                    tag.is_signed = false;
                    tag.spelling = model.kind == K::Variant ? "unsigned long long" : "bool";
                    converted.projections.push_back(tag);
                }
                for (int i = 0; i < count; ++i) {
                    CXType argument = clang_Type_getTemplateArgumentAsType(canonical, static_cast<unsigned>(i));
                    if (model.kind == K::Expected && i == 0 && argument.kind == CXType_Void) {
                        Type empty;
                        empty.kind = TypeKind::Value;
                        empty.spelling = "void";
                        empty.representation.identity = "unit";
                        converted.projections.push_back(empty);
                        model.components.push_back({"value", {}, true});
                    } else {
                        component(argument, model.kind == K::Pair ? (i == 0 ? "first" : "second") : std::to_string(i),
                                  declaration);
                    }
                }
            } else {
                if (clang_getCursorKind(definition) == CXCursor_UnionDecl) {
                    model.rejection = "a union requires an independently justified active-member model";
                    break;
                }
                if (record_has_base(canonical)) {
                    model.rejection = "base subobject decomposition requires an explicit accessible projection";
                    break;
                }
                // A destructor runs where an object's lifetime ends, at a scope
                // exit no statement names, and what it does is not modeled
                // (SPEC.md CLASS-015).
                if (has_user_provided_destructor(definition)) {
                    model.rejection = "it has a user-provided destructor, which runs where an object's lifetime ends "
                                      "and whose effects are not modeled (SPEC.md CLASS-015)";
                    break;
                }
                for (const auto& field : record_fields(canonical))
                    component(clang_getCursorType(field), take(clang_getCursorSpelling(field)), field,
                              clang_getCXXAccessSpecifier(field) == CX_CXXPublic);
            }
            break;
        }
        case CXType_Enum: {
            // An enumeration holds a value of its underlying type. A scoped one,
            // or an unscoped one with a fixed underlying type, holds exactly that
            // type's values; an unscoped one without a fixed type holds a subset
            // of them, so reading it as the whole type asks more, never less.
            // Its enumerators are its named states either way (SPEC.md 20.1).
            const CXCursor declaration = clang_getTypeDeclaration(canonical);
            const CXCursor definition = clang_getCursorDefinition(declaration);
            const bool opaque_enumeration = clang_Cursor_isNull(definition) != 0;
            if (opaque_enumeration)
                break;
            const Type underlying = convert_type(clang_getEnumDeclIntegerType(declaration));
            // Bool-backed and wide enums remain outside this initial model.
            if (underlying.kind != TypeKind::Int)
                break;
            std::vector<Enumerator> enumerators;
            for (const CXCursor& child : children_of(definition)) {
                if (clang_getCursorKind(child) != CXCursor_EnumConstantDecl)
                    continue;
                enumerators.push_back(
                    Enumerator{take(clang_getCursorSpelling(child)), enumerator_value(child, underlying.is_signed)});
            }
            converted.kind = underlying.kind;
            converted.width = underlying.width;
            converted.is_signed = underlying.is_signed;
            converted.representation.identity = take(clang_getCursorUSR(declaration));
            converted.representation.name = take(clang_getTypeSpelling(canonical));
            converted.representation.enumerators = std::move(enumerators);
            converted.representation.kind = source::RepresentationKind::ScopedEnum;
            break;
        }
        case CXType_Bool:
            converted.kind = TypeKind::Bool;
            break;

        case CXType_Char_S:
        case CXType_SChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long:
        case CXType_LongLong:
            size = clang_Type_getSizeOf(canonical);
            if (size > 0 && size <= 8) {
                converted.kind = TypeKind::Int;
                converted.is_signed = true;
                converted.width = static_cast<std::uint16_t>(size * 8);
            }
            break;

        case CXType_Char_U:
        case CXType_UChar:
        case CXType_UShort:
        case CXType_UInt:
        case CXType_ULong:
        case CXType_ULongLong:
            size = clang_Type_getSizeOf(canonical);
            if (size > 0 && size <= 8) {
                converted.kind = TypeKind::Int;
                converted.is_signed = false;
                converted.width = static_cast<std::uint16_t>(size * 8);
            }
            break;

        default:
            break;
    }

    return converted;
}

std::string qualified_name_of(CXCursor cursor) {
    std::vector<std::string> parts;
    parts.push_back(take(clang_getCursorSpelling(cursor)));

    CXCursor parent = clang_getCursorSemanticParent(cursor);
    while (!clang_Cursor_isNull(parent) && clang_getCursorKind(parent) != CXCursor_TranslationUnit &&
           !clang_isInvalid(clang_getCursorKind(parent))) {
        std::string name = take(clang_getCursorSpelling(parent));
        if (!name.empty()) {
            parts.push_back(std::move(name));
        }
        const CXCursor next = clang_getCursorSemanticParent(parent);
        if (clang_equalCursors(next, parent) != 0) {
            break;
        }
        parent = next;
    }

    std::string qualified;
    for (auto& part : std::views::reverse(parts)) {
        if (!qualified.empty()) {
            qualified += "::";
        }
        qualified += part;
    }
    return qualified;
}

Expr unsupported_expression(CXCursor cursor, std::string reason) {
    Expr expr;
    expr.type = convert_type(clang_getCursorType(cursor));
    expr.location = presumed_location(clang_getCursorLocation(cursor));
    expr.node = Unsupported{std::move(reason)};
    return expr;
}

} // namespace cppl::clangbridge::detail
