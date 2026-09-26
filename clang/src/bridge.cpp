#include "cppl/clang/bridge.hpp"

#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/projection.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"

#include <algorithm>
#include <clang-c/CXDiagnostic.h>
#include <clang-c/CXErrorCode.h>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/CXString.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::clangbridge {

namespace {

constexpr unsigned kMaxExpressionDepth = 128;
constexpr std::size_t kMaxReturnPaths = 128;
// A condition's operators nest, and each `&&`/`||` places its second operand on
// a further route, so elaboration is bounded as expression depth is.
constexpr unsigned kMaxConditionDepth = 64;
// An aggregate's members may themselves be aggregates, so one declaration can
// establish many places. Both the nesting and the total are bounded: products
// multiply, and a deeply nested array of arrays would otherwise ask for more
// versions than a proof can carry (SPEC.md 12.10).
constexpr std::size_t kMaxPlaceDepth = 8;
constexpr std::size_t kMaxTrackedLeaves = 256;

class ScopedString {
  public:
    explicit ScopedString(CXString value) : value_(value) {}
    ~ScopedString() {
        clang_disposeString(value_);
    }

    ScopedString(const ScopedString&) = delete;
    ScopedString& operator=(const ScopedString&) = delete;
    ScopedString(ScopedString&&) = delete;
    ScopedString& operator=(ScopedString&&) = delete;

    [[nodiscard]] std::string str() const {
        const char* text = clang_getCString(value_);
        return text != nullptr ? std::string(text) : std::string();
    }

  private:
    CXString value_;
};

std::string take(CXString value) {
    return ScopedString(value).str();
}

source::SourceLocation presumed_location(CXSourceLocation location) {
    CXString file{};
    unsigned line = 0;
    unsigned column = 0;
    clang_getPresumedLocation(location, &file, &line, &column);

    source::SourceLocation result;
    result.file = take(file);
    result.line = line;
    result.column = column;
    return result;
}

// Where a cursor stands, as a diagnostic names it.
std::string describe_location(CXCursor at) {
    const source::SourceLocation where = presumed_location(clang_getCursorLocation(at));
    return where.file + ":" + std::to_string(where.line);
}

std::vector<CXCursor> children_of(CXCursor cursor) {
    std::vector<CXCursor> children;
    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor, CXClientData data) {
            static_cast<std::vector<CXCursor>*>(data)->push_back(child);
            return CXChildVisit_Continue;
        },
        &children);
    return children;
}

// The data members of a record type, in declaration order.
//
// This asks the type rather than walking the definition's cursor children,
// because an instantiated class template specialization has no children: Clang
// instantiates the members without exposing cursors for them, so a cursor walk
// reports a specialization as having no members at all (SPEC.md TEMPLATE-001).
// A record and an instantiation of a class template are the same kind of
// product here, so both are decomposed by the one route.
std::vector<CXCursor> record_fields(CXType record) {
    std::vector<CXCursor> fields;
    clang_Type_visitFields(
        clang_getCanonicalType(record),
        [](CXCursor field, CXClientData data) {
            static_cast<std::vector<CXCursor>*>(data)->push_back(field);
            return CXVisit_Continue;
        },
        &fields);
    return fields;
}

// Whether a record type has any base subobject.
//
// A base carries state that `record_fields` does not report, so a record with
// one is not decomposed by its members alone. Asking the type matters for the
// same reason: an instantiation exposes no base-specifier cursor either, so a
// cursor walk would report a derived specialization as having no base and would
// silently model it as its own members (AGENTS.md 8).
bool record_has_base(CXType record) {
    unsigned bases = 0;
    clang_visitCXXBaseClasses(
        clang_getCanonicalType(record),
        [](CXCursor, CXClientData data) {
            ++*static_cast<unsigned*>(data);
            return CXVisit_Break;
        },
        &bases);
    return bases != 0;
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

// Whether a reference type is read through to its referent.
//
// A reference is not a value: reading one is an access to another object that
// other code may write. Treating `T&` as `T` everywhere would let a contract be
// proven about a parameter whose value can change under it (AGENTS.md 11), so
// the referent is read only where the caller has established that the subject's
// logical value is the one being reasoned about.
enum class ReferenceModel : std::uint8_t {
    Opaque,
    Referent,
};

std::expected<std::vector<Refinement>, std::string> refinements_of(CXCursor declared, CXType written,
                                                                   const std::vector<Selection::Refinement>& known);

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
Type convert_type(CXType type, unsigned depth = 0, ReferenceModel references = ReferenceModel::Opaque,
                  const std::vector<Selection::Refinement>* known = nullptr) {
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
                        model.rejection = "component '" + name + "' has " + member_refinements.error();
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
                for (const auto& field : record_fields(canonical))
                    component(clang_getCursorType(field), take(clang_getCursorSpelling(field)), field,
                              clang_getCXXAccessSpecifier(field) == CX_CXXPublic);
            }
            break;
        }
        case CXType_Enum: {
            const CXCursor declaration = clang_getTypeDeclaration(canonical);
            const CXCursor definition = clang_getCursorDefinition(declaration);
            if (clang_EnumDecl_isScoped(declaration) == 0 || clang_Cursor_isNull(definition) != 0)
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

// The refinements a declaration's written type names, outermost first (SPEC.md
// 17, 18).
//
// Clang canonicalizes `Percentage` to `int`, which is exactly right for the
// runtime program and loses the verification-level identity, so the alias
// declaration the type came through is what names it here. A refinement of a
// refinement contributes every predicate that applies to the value, because each
// alias is followed to the type it stands for.
//
// An indexed refinement was applied at values rather than at types, and those
// values are not reachable through the type. They stand as the declaration's own
// leading children, after the reference to the alias template, where Clang has
// already evaluated them.
std::size_t physical_offset(CXCursor cursor);

std::vector<std::int64_t> refinement_arguments(CXCursor declared) {
    std::vector<std::int64_t> arguments;
    const CXCursor initializer = clang_Cursor_getVarDeclInitializer(declared);
    for (const CXCursor child : children_of(declared)) {
        if (!clang_Cursor_isNull(initializer) && clang_equalCursors(child, initializer))
            break;
        const auto kind = clang_getCursorKind(child);
        if (kind == CXCursor_TemplateRef || kind == CXCursor_TypeRef || kind == CXCursor_NamespaceRef)
            continue;
        if (clang_isDeclaration(kind) || clang_isStatement(kind))
            break;
        if (CXEvalResult evaluated = clang_Cursor_Evaluate(child)) {
            const bool integral = clang_EvalResult_getKind(evaluated) == CXEval_Int;
            const auto value = integral ? clang_EvalResult_getAsLongLong(evaluated) : 0;
            clang_EvalResult_dispose(evaluated);
            if (integral) {
                arguments.push_back(static_cast<std::int64_t>(value));
                continue;
            }
        }
        break;
    }
    return arguments;
}

std::expected<std::vector<Refinement>, std::string> refinements_of(CXCursor declared, CXType written,
                                                                   const std::vector<Selection::Refinement>& known) {
    std::vector<Refinement> found;
    if (known.empty())
        return found;
    auto arguments = refinement_arguments(declared);
    std::vector<CXCursor> visited;
    for (unsigned step = 0; step < kMaxExpressionDepth; ++step) {
        CXCursor declaration = clang_getTypeDeclaration(written);
        if (clang_getCursorKind(declaration) == CXCursor_TypeAliasTemplateDecl) {
            const auto children = children_of(declaration);
            const auto alias = std::ranges::find_if(
                children, [](CXCursor child) { return clang_getCursorKind(child) == CXCursor_TypeAliasDecl; });
            if (alias == children.end())
                return std::unexpected("refinement alias template has no resolved alias declaration");
            declaration = *alias;
        }
        const CXCursorKind kind = clang_getCursorKind(declaration);
        if (kind != CXCursor_TypeAliasDecl && kind != CXCursor_TypedefDecl && kind != CXCursor_TypeAliasTemplateDecl)
            return found;
        if (std::ranges::any_of(visited, [&](CXCursor previous) { return clang_equalCursors(previous, declaration); }))
            return std::unexpected("cyclic refinement alias metadata");
        visited.push_back(declaration);
        // The projector records the generated alias's physical identity. Source
        // spelling and presumed #line locations cannot identify a refinement.
        const auto entry = std::ranges::find(known, physical_offset(declaration), &Selection::Refinement::alias_offset);
        if (entry != known.end()) {
            if (entry->index_count != arguments.size())
                return std::unexpected("refinement '" + entry->name + "' has unresolved index arguments");
            found.push_back(Refinement{entry->name, arguments, entry->probe});
        }
        const CXType underlying = clang_getTypedefDeclUnderlyingType(declaration);
        if (underlying.kind == CXType_Invalid) {
            if (kind == CXCursor_TypeAliasTemplateDecl)
                return std::unexpected("dependent refinement alias substitution is not resolved by the Clang bridge");
            return found;
        }
        written = underlying;
        // An ordinary alias may name an indexed refinement. Read that alias's
        // resolved application, not the initializer or a previous alias's indices.
        arguments = refinement_arguments(declaration);
    }
    return std::unexpected("refinement alias chain exceeds the analysis limit");
}

bool same_term(const Expr& lhs, const Expr& rhs);

// The element type of a modeled sequence as `written` spells it, with the
// refinements that spelling names (SPEC.md 17.6, RFC 0020 §6).
//
// Clang canonicalizes `std::vector<Positive>` to `std::vector<int>`, which is
// the runtime type and loses what verification needs, so the written type is
// followed through its aliases to the specialization as written, whose first
// argument keeps the alias a refinement is named by. A spelling this cannot
// follow is refused rather than read as an unrefined element.
std::expected<Type, std::string> sequence_element(CXCursor declared, CXType written,
                                                  const std::vector<Selection::Refinement>* known) {
    for (unsigned step = 0; step < kMaxExpressionDepth; ++step) {
        if (written.kind == CXType_LValueReference || written.kind == CXType_RValueReference) {
            written = clang_getPointeeType(written);
            continue;
        }
        if (clang_Type_getNumTemplateArguments(written) >= 1) {
            const CXType element = clang_Type_getTemplateArgumentAsType(written, 0);
            if (element.kind == CXType_Invalid) {
                break;
            }
            Type converted = convert_type(element);
            if (converted.kind != TypeKind::Int && converted.kind != TypeKind::Bool) {
                return std::unexpected("its element type '" + converted.spelling + "' is not modeled");
            }
            if (known != nullptr) {
                auto refinements = refinements_of(declared, element, *known);
                if (!refinements) {
                    return std::unexpected("its element type has " + refinements.error());
                }
                converted.refinements = std::move(*refinements);
            }
            return converted;
        }
        if (written.kind == CXType_Elaborated) {
            written = clang_Type_getNamedType(written);
            continue;
        }
        const CXType underlying = clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(written));
        if (underlying.kind == CXType_Invalid) {
            break;
        }
        written = underlying;
    }
    return std::unexpected("its element type could not be read from how its type is written");
}

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

// A member of a modeled sequence as a diagnostic and the trust report name it:
// the standard name, without the inline namespace a library puts it in, so the
// same program is described alike whichever library it is compiled against.
std::string library_name(source::RepresentationKind family, const std::string& member) {
    return std::string(source::describe_model(family)) + "::" + member;
}

std::optional<SequenceCall> sequence_call(CXCursor cursor) {
    if (clang_getCursorKind(cursor) != CXCursor_CallExpr) {
        return std::nullopt;
    }
    const CXCursor method = clang_getCursorReferenced(cursor);
    const CXCursorKind kind = clang_getCursorKind(method);
    if (kind != CXCursor_CXXMethod && kind != CXCursor_Constructor) {
        return std::nullopt;
    }
    const source::RepresentationKind family = library_kind(clang_getCursorSemanticParent(method));
    if (!source::is_sequence(family) && family != source::RepresentationKind::StdArray) {
        return std::nullopt;
    }
    SequenceCall call;
    call.method = method;
    call.family = family;
    call.name = take(clang_getCursorSpelling(method));
    call.constructor = kind == CXCursor_Constructor;
    const int count = clang_Cursor_getNumArguments(cursor);
    if (count < 0) {
        return std::nullopt;
    }
    std::size_t first = 0;
    if (!call.constructor) {
        const std::vector<CXCursor> children = children_of(cursor);
        if (children.empty()) {
            return std::nullopt;
        }
        if (clang_getCursorKind(children.front()) == CXCursor_MemberRefExpr &&
            clang_equalCursors(clang_getCursorReferenced(children.front()), method) != 0) {
            const std::vector<CXCursor> member = children_of(children.front());
            if (member.size() != 1) {
                return std::nullopt;
            }
            call.object = member.front();
        } else {
            // An operator's object is its first argument.
            if (count < 1) {
                return std::nullopt;
            }
            call.object = clang_Cursor_getArgument(cursor, 0);
            first = 1;
        }
    }
    for (auto index = static_cast<unsigned>(first); index < static_cast<unsigned>(count); ++index) {
        call.arguments.push_back(clang_Cursor_getArgument(cursor, index));
    }
    return call;
}

// One tracked place: storage a verified body can read and write under logical
// versioning (SPEC.md 12.10, RFC 0014 §1).
//
// A place's identity is the declaration Clang resolved plus the path of
// projections taken into it, so `s` and `s.x` and `s.x.y` are three places of
// one object and `s.x` and `s.y` are never the same place. Shadowing needs no
// rule of its own, because an inner declaration is a different declaration.
//
// An aggregate local is tracked as one place per modeled member rather than as
// a single value, because a structural value has components instead of the one
// modeled value a version can denote.
struct Local {
    CXCursor declaration = clang_getNullCursor();
    std::uint32_t version = 0;
    Type type;
    std::optional<std::size_t> referent = std::nullopt;
    bool external = false; // may alias another reference parameter
    std::vector<PlaceStep> path;
    std::string spelling; // how this place is written, for diagnostics

    // The pointee of a pointer, rather than storage a declaration names. The
    // entry holding the pointer is what identifies it, together with the
    // version of that pointer this dereference read: `*p` before and after a
    // write to `p` are different places (RFC 0014 §1). `declaration` is the
    // pointer's declaration so lookups that key on it keep working.
    std::optional<std::size_t> pointer = std::nullopt;
    std::uint32_t pointer_version = 0;

    // Whether this entry is a symbolic element place: an element whose index is
    // a term, so which element it selects is not decided here.
    //
    // This is its own flag rather than a property read off the extent. "Is this
    // place symbolic" and "does an extent term exist for it" are different
    // questions, and once the extent is a term the second can fail
    // independently; a sentinel would make a failed extent indistinguishable
    // from an ordinary element.
    bool symbolic = false;

    // For a symbolic element place, the extent of the array it indexes. The
    // index owes `index < extent`, which is a proposition about values and so
    // is proved by the kernel rather than tracked (RFC 0014 §10, §17 step 7).
    //
    // A term, not a count: `readable(p, n)` bounds a region by a value that is
    // never a literal, and no enumeration of elements can recover it (SPEC.md
    // 12.10, STORAGE-005).
    std::vector<Expr> extent;

    // The index value, lowered where the place was formed so it denotes the
    // versions current there. A vector because `Expr` is incomplete here.
    std::vector<Expr> index_value;

    // A binder of an arm of a case split on this path, rather than storage: a
    // name for the value the arm's case exposes. It has no version, is never
    // written, and a read of it is that value (SPEC.md CASE-017).
    std::optional<CaseBinder> binder = std::nullopt;

    // Storage this body reads and never writes: a whole object a parameter
    // designates by reference. Its version follows what may have been written
    // to it -- through another reference, a member of the implicit object, a
    // call or an unsafe block -- so a read after such a write is of a value
    // nothing states rather than of the one it arrived with (SPEC.md 12.9,
    // CLASS-010). A write to it is refused, since its post-state would then
    // be a value this body cannot state member by member.
    bool read_only = false;

    // The root of a modeled sequence (RFC 0020 §3): the entry whose versions
    // carry a vector's, a string's or a span's abstract value, its length. The
    // version of a root is the storage generation of what it owns or views
    // (§4): every operation that may reallocate, shrink, replace or end that
    // storage establishes a new one, and an element write does not.
    struct Sequence {
        source::RepresentationKind kind = source::RepresentationKind::None;
        // The element type, with the refinements the declaration names. For a
        // span, that of the container it views, since the elements are that
        // container's storage.
        Type element;
        // Whether the elements are storage a caller owns: those of a container
        // bound by reference, whatever this body can prove about the object.
        bool external_elements = false;
        // For a span local, the root of the container whose storage it views.
        std::optional<std::size_t> views = std::nullopt;
        // What established the current generation, for the diagnostic that a
        // view or reference formed before it is used after it.
        std::string invalidated;
    };
    std::optional<Sequence> sequence = std::nullopt;

    // Where an entry depends on a sequence's storage generation: the root it
    // belongs to and the generation it was formed at (RFC 0020 §4).
    struct Generation {
        std::size_t root = 0;
        std::uint32_t version = 0;
    };

    // An element place of a sequence: formed at a generation, it is the place a
    // subscript names only while that generation is current. After it changes,
    // the next access forms a new place, which owes its bound again.
    std::optional<Generation> formed_at = std::nullopt;

    // A span local, or a reference bound to a sequence element: it designates
    // storage formed at a generation, and using it at any other one is using
    // storage that may no longer exist, which is refused (STDMODEL-015).
    std::optional<Generation> borrows = std::nullopt;

    [[nodiscard]] bool is_deref() const {
        return pointer.has_value();
    }

    // Distinct members of one object are distinct storage, so a write to one
    // leaves the others alone. This is the only disjointness concluded here,
    // and it comes from Clang's resolved member identity (AGENTS.md storage
    // invariants): never from a type-based aliasing argument.
    // A symbolic step records only that some index was a term, not which one, so
    // a path alone does not tell `a[i]` from `a[j]`. Such a place is recognized
    // only when the index term is supplied and is the same term: without it the
    // entry is not this place, and the access forms its own.
    bool same_place(CXCursor object, const std::vector<PlaceStep>& projection, const Expr* index_term) const {
        if (is_deref() || clang_equalCursors(declaration, object) == 0 || path != projection) {
            return false;
        }
        if (!has_symbolic_step()) {
            return true;
        }
        return index_term != nullptr && !index_value.empty() && same_term(index_value.front(), *index_term);
    }

    // Whether any step of this place's path is a symbolic element, which makes
    // the place undecided: which element it selects is not known here, so it is
    // never concluded disjoint from a sibling element.
    [[nodiscard]] bool has_symbolic_step() const {
        return std::ranges::any_of(path,
                                   [](const PlaceStep& step) { return step.kind == PlaceStep::Kind::SymbolicElement; });
    }

    // Whether a write to `other` reaches this place: `s` covers `s.x`, and
    // `s.x` covers neither `s.y` nor `s`.
    //
    // A dereference is covered only by a dereference of the same pointer
    // version. Two dereferences of *different* pointers are not concluded
    // disjoint here: that is decided by `may_alias`, which must assume they
    // overlap (RFC 0014 §4).
    [[nodiscard]] bool covered_by(const Local& other) const {
        if (is_deref() != other.is_deref()) {
            return false;
        }
        if (is_deref() && (pointer != other.pointer || pointer_version != other.pointer_version)) {
            return false;
        }
        if (!is_deref() && clang_equalCursors(declaration, other.declaration) == 0) {
            return false;
        }
        if (other.path.size() > path.size()) {
            return false;
        }
        return std::equal(other.path.begin(), other.path.end(), path.begin());
    }
};

using Locals = std::vector<Local>;

// Whether two entries are distinct members of one object: rooted in the same
// declaration, and apart at a step both paths decide. Distinct members of one
// object are distinct storage, which Clang resolves (RFC 0014 §4, SPEC.md
// CLASS-010). A symbolic step decides nothing, so an element selected at a
// term is never distinct from a sibling element.
bool distinct_members(const Local& lhs, const Local& rhs) {
    if (lhs.is_deref() || rhs.is_deref() || clang_equalCursors(lhs.declaration, rhs.declaration) == 0 ||
        lhs.has_symbolic_step() || rhs.has_symbolic_step()) {
        return false;
    }
    return !lhs.covered_by(rhs) && !rhs.covered_by(lhs);
}

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
    return canonical.kind == CXType_Pointer && clang_isConstQualifiedType(clang_getPointeeType(canonical)) == 0U;
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

std::optional<std::size_t> find_binding(const Locals& locals, CXCursor declaration,
                                        const std::vector<PlaceStep>& path = {}, const Expr* index_term = nullptr) {
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

std::optional<std::size_t> find_local(const Locals& locals, CXCursor declaration,
                                      const std::vector<PlaceStep>& path = {}, const Expr* index_term = nullptr) {
    const auto binding = find_binding(locals, declaration, path, index_term);
    return binding ? std::optional{locals[*binding].referent.value_or(*binding)} : std::nullopt;
}

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

// One scalar place of a member function's implicit object: a data member of the
// class, or a member or an element of one, reached from the object by `path`
// (SPEC.md CLASS-008). The path numbers members the way `field_index_of` does,
// so `this->a.b`, `(*this).a.b` and `a.b` in the member function all resolve to
// exactly this leaf.
struct ReceiverLeaf {
    std::vector<PlaceStep> path;
    Type type; // with the refinement the member declared (SPEC.md 17.6)
    std::string spelling;
    // Reached through a `mutable` member, which a `const` member function may
    // still write (SPEC.md CONTRACT-010, CLASS-009).
    bool mutable_member = false;
};

// The implicit object of a non-static member function, as the verified callable
// sees it (SPEC.md CLASS-008): the class the function belongs to, and the
// object's scalar leaves, each a reference parameter standing before the
// written parameters.
//
// A member whose type this implementation does not model -- a pointer, a
// floating-point value, a library type, a class with a base -- has no leaf. It
// is storage a verified body can neither read nor write, so nothing is known or
// claimed about it, and a body that names it is refused where it does.
struct Receiver {
    CXCursor record = clang_getNullCursor(); // canonical class declaration
    std::vector<ReceiverLeaf> leaves;
    bool constant = false; // a `const` member function
    bool rvalue = false;   // one whose ref-qualifier is `&&`

    // How the member function binds each leaf (SPEC.md CLASS-009): a `const`
    // one reads its object and may write only a `mutable` member of it.
    [[nodiscard]] source::ParameterPassing passing(const ReceiverLeaf& leaf) const {
        if (constant && !leaf.mutable_member) {
            return source::ParameterPassing::ConstReference;
        }
        return rvalue ? source::ParameterPassing::RvalueReference : source::ParameterPassing::MutableReference;
    }

    // The position of the leaf at `path`, which is its parameter position.
    [[nodiscard]] std::optional<std::size_t> leaf_at(const std::vector<PlaceStep>& path) const {
        for (std::size_t index = 0; index < leaves.size(); ++index) {
            if (leaves[index].path == path) {
                return index;
            }
        }
        return std::nullopt;
    }

    // Whether the member function may write any leaf.
    [[nodiscard]] bool writes() const {
        return std::ranges::any_of(leaves,
                                   [this](const ReceiverLeaf& leaf) { return source::may_write(passing(leaf)); });
    }
};

// The canonical declaration of the class `member` is declared in: the identity
// the implicit object of a member function, and a member named without an
// object, resolve to.
CXCursor enclosing_record(CXCursor member) {
    return clang_getCanonicalCursor(clang_getCursorSemanticParent(member));
}

// The scalar leaves of the storage `path` names, of written type `written`,
// appended to `leaves` in declaration order (SPEC.md CLASS-008). A member that
// is itself a record or an array is followed into its own members and elements,
// so `this->a.b` and `this->items[2].v` are places exactly as `this->a` is. A
// member this implementation does not model is left without a leaf, and so is
// storage nested past the depth places are tracked to. `field` is the data
// member the storage belongs to, whose declaration names any refinement.
std::optional<std::string> collect_receiver_leaves(CXType written, CXCursor field, const std::vector<PlaceStep>& path,
                                                   const std::string& spelling, bool mutable_member,
                                                   const std::vector<Selection::Refinement>* known,
                                                   std::vector<ReceiverLeaf>& leaves) {
    if (path.size() > kMaxPlaceDepth) {
        return std::nullopt;
    }
    const CXType canonical = clang_getCanonicalType(written);
    if (canonical.kind == CXType_Record) {
        const CXCursor declaration = clang_getTypeDeclaration(canonical);
        const CXCursor definition = clang_getCursorDefinition(declaration);
        if (clang_Cursor_isNull(definition) != 0 || clang_getCursorKind(definition) == CXCursor_UnionDecl ||
            record_has_base(canonical) || library_kind(declaration) != source::RepresentationKind::Record) {
            return std::nullopt;
        }
        const auto fields = record_fields(canonical);
        for (std::size_t index = 0; index < fields.size(); ++index) {
            // A bit-field has no place of its own (`field_index_of`).
            if (clang_getFieldDeclBitWidth(fields[index]) >= 0) {
                continue;
            }
            std::vector<PlaceStep> member = path;
            member.push_back(PlaceStep{PlaceStep::Kind::Field, static_cast<std::uint32_t>(index)});
            const std::string name =
                spelling + (path.empty() ? "" : ".") + take(clang_getCursorSpelling(fields[index]));
            if (auto refused = collect_receiver_leaves(clang_getCursorType(fields[index]), fields[index], member, name,
                                                       mutable_member || clang_CXXField_isMutable(fields[index]) != 0,
                                                       known, leaves)) {
                return refused;
            }
        }
        return std::nullopt;
    }
    if (canonical.kind == CXType_ConstantArray) {
        const long long count = clang_getArraySize(canonical);
        if (count < 0 || count > static_cast<long long>(kMaxTrackedLeaves)) {
            return std::nullopt;
        }
        // The element type is taken from the written array type, so a
        // refinement the element type names is kept (SPEC.md 17.3).
        CXType element = clang_getArrayElementType(written);
        if (element.kind == CXType_Invalid) {
            element = clang_getArrayElementType(canonical);
        }
        for (long long position = 0; position < count; ++position) {
            std::vector<PlaceStep> at = path;
            at.push_back(PlaceStep{PlaceStep::Kind::Element, static_cast<std::uint32_t>(position)});
            if (auto refused =
                    collect_receiver_leaves(element, field, at, spelling + "[" + std::to_string(position) + "]",
                                            mutable_member, known, leaves)) {
                return refused;
            }
        }
        return std::nullopt;
    }
    Type converted = convert_type(written, 0, ReferenceModel::Opaque, known);
    if (path.empty() || (converted.kind != TypeKind::Int && converted.kind != TypeKind::Bool)) {
        return std::nullopt;
    }
    if (known != nullptr) {
        auto refinements = refinements_of(field, written, *known);
        if (!refinements) {
            return "member '" + spelling + "' has " + refinements.error();
        }
        converted.refinements = std::move(*refinements);
    }
    if (leaves.size() >= kMaxTrackedLeaves) {
        return "the object has more members than the proof resource limit allows";
    }
    leaves.push_back(ReceiverLeaf{path, std::move(converted), spelling, mutable_member});
    return std::nullopt;
}

// The implicit object of `method`, or why its object is not one this
// implementation models (SPEC.md CLASS-008, CLASS-015). A static member
// function has none, and is not asked.
std::expected<Receiver, std::string> receiver_of(CXCursor method, const std::vector<Selection::Refinement>* known) {
    Receiver receiver;
    receiver.record = enclosing_record(method);
    const CXCursorKind kind = clang_getCursorKind(receiver.record);
    if (kind == CXCursor_UnionDecl) {
        return std::unexpected("its class is a union, whose active member is not modeled");
    }
    if (kind != CXCursor_StructDecl && kind != CXCursor_ClassDecl) {
        return std::unexpected("it is not a member of a class this implementation models");
    }
    const CXType type = clang_getCanonicalType(clang_getCursorType(receiver.record));
    if (clang_Cursor_isNull(clang_getCursorDefinition(receiver.record)) != 0) {
        return std::unexpected("its class is incomplete");
    }
    if (record_has_base(type)) {
        return std::unexpected("its class has a base subobject, whose storage and dispatch the receiver model does not "
                               "follow");
    }
    receiver.constant = clang_CXXMethod_isConst(method) != 0;
    receiver.rvalue = clang_Type_getCXXRefQualifier(clang_getCursorType(method)) == CXRefQualifier_RValue;
    if (auto refused =
            collect_receiver_leaves(type, clang_getNullCursor(), {}, "this->", false, known, receiver.leaves)) {
        return std::unexpected(*refused);
    }
    return receiver;
}

// What a body or a clause is lowered against: the parameters Clang resolved for
// its declaration and, for a non-static member function, its implicit object.
// The verified callable takes the implicit object's leaves first and the
// written parameters after them, so a written parameter stands at its own
// position moved past the leaves (SPEC.md CLASS-008).
struct Signature {
    std::vector<CXCursor> parameters;
    std::optional<Receiver> receiver;
    // A clause states no body in which a leaf is written, so it reads each leaf
    // as the parameter it is. A body tracks every leaf as storage instead, and
    // reads it at the version current where the read stands.
    bool clause = false;

    [[nodiscard]] std::uint32_t leaves() const {
        return receiver.has_value() ? static_cast<std::uint32_t>(receiver->leaves.size()) : 0;
    }

    // The callable position of written parameter `index`.
    [[nodiscard]] std::uint32_t position(std::size_t index) const {
        return leaves() + static_cast<std::uint32_t>(index);
    }

    // The written parameter `declaration` names, by its callable position.
    [[nodiscard]] std::optional<std::uint32_t> position_of(CXCursor declaration) const {
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (clang_equalCursors(parameters[index], declaration) != 0) {
                return position(index);
            }
        }
        return std::nullopt;
    }
};

CXCursor strip_parens(CXCursor cursor) {
    while (clang_getCursorKind(cursor) == CXCursor_UnexposedExpr || clang_getCursorKind(cursor) == CXCursor_ParenExpr) {
        const auto inner = children_of(cursor);
        if (inner.size() != 1)
            break;
        cursor = inner[0];
    }
    return cursor;
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

// The place an access expression names: the object it is ultimately rooted in,
// and the path of projections taken into it.
//
// This is the one resolver for every access form. `s`, `s.x`, `s.x.y`, `a[1]`
// and `a[1].x` all walk the same chain, so a member of a member is an ordinary
// place rather than a special case, and no access form gets a resolution rule
// of its own (AGENTS.md storage invariants). Resolution is by Clang's member
// identity, so any spelling of one member is one place.
//
// The chain is walked outermost-first and the path is reversed at the end,
// because `s.x.y` is a member access `y` whose object is a member access `x`.
struct ResolvedAccess {
    CXCursor object;
    std::vector<PlaceStep> path;

    // Whether the access goes through a dereference of `object`, which must
    // hold a pointer. `*p`, `p->m` and `p[i]` all resolve this way, so one
    // capability rule and one read/write path serve all three.
    bool dereferenced = false;

    // The index expressions of the symbolic element steps in `path`, in the
    // order those steps appear. Each one owes a bounds obligation against its
    // array's extent, and the obligation is a proposition about values, so it
    // is proved by the kernel rather than tracked (RFC 0014 §10).
    std::vector<CXCursor> symbolic_indices;

    // Whether the access is rooted in the implicit object of the member
    // function it stands in: `this->x`, `(*this).x`, or `x` written alone
    // (SPEC.md CLASS-008). `this` is a pointer, but it is not a pointer the
    // body dereferences: it designates the object the function was called on,
    // which is the receiver's own storage, so no capability is owed to reach it.
    bool receiver = false;

    // The declaration the place is rooted in: the one `object` names, or for
    // the implicit object the canonical declaration of its class.
    CXCursor declaration = clang_getNullCursor();
};

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

bool same_terms(const std::vector<Expr>& lhs, const std::vector<Expr>& rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin(), same_term);
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
Expr build_expression(CXCursor cursor, const Signature& signature, const Locals& locals, unsigned depth,
                      bool sequenced_call = false);

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

// Why an access rooted in the implicit object names no leaf of the receiver
// `signature` has, or nothing when it names one.
std::optional<std::string> receiver_gap(const ResolvedAccess& access, const Signature& signature) {
    if (!signature.receiver.has_value()) {
        return "'this' names no object here: only a non-static member function has an implicit object";
    }
    if (clang_equalCursors(access.declaration, signature.receiver->record) == 0) {
        return "this member belongs to a class other than the one this member function is declared in, and a base "
               "subobject is not modeled";
    }
    if (!signature.receiver->leaf_at(access.path).has_value()) {
        return "this member of the implicit object is not tracked storage: its type is not one this implementation "
               "models";
    }
    return std::nullopt;
}

// A read of the implicit object's storage (SPEC.md CLASS-008).
//
// A body tracks every leaf of its receiver as storage from entry, so it reads
// one at the version current where the read stands, through the one read path.
// A clause states no body: in it, a leaf is the parameter it stands for, at the
// state the clause describes -- entry for a precondition, the normal-return
// post-state for a postcondition (SPEC.md CONTRACT-009).
Expr read_receiver(const ResolvedAccess& access, CXCursor cursor, const Signature& signature, const Locals& locals) {
    if (!signature.clause) {
        if (const auto entry = tracked_place(cursor, locals, signature)) {
            return read_place(locals, *entry, cursor);
        }
        return unsupported_expression(cursor, receiver_gap(access, signature)
                                                  .value_or("this member of the implicit object is not tracked "
                                                            "storage of this body"));
    }
    if (!access.symbolic_indices.empty()) {
        return unsupported_expression(cursor, "a contract does not select an element of a member array at a term; "
                                              "the implicit object's elements are parameters of their own");
    }
    if (const auto gap = receiver_gap(access, signature)) {
        return unsupported_expression(cursor, *gap);
    }
    const std::optional<std::size_t> leaf =
        signature.receiver.has_value() ? signature.receiver->leaf_at(access.path) : std::nullopt;
    if (!leaf.has_value()) {
        return unsupported_expression(cursor, "this member of the implicit object is not tracked storage");
    }
    Expr expr;
    expr.type = convert_type(clang_getCursorType(cursor));
    expr.location = presumed_location(clang_getCursorLocation(cursor));
    expr.node = ParameterRef{static_cast<std::uint32_t>(*leaf), signature.receiver->leaves[*leaf].spelling};
    return expr;
}

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

// The object a member function call is made on, as storage the caller can name
// (SPEC.md CLASS-011): the declaration it is rooted in -- the class of the
// caller's own implicit object, for `this` -- and the path of members and
// elements from there to the object.
struct CallObject {
    CXCursor declaration = clang_getNullCursor();
    std::vector<PlaceStep> path;
    bool receiver = false;
};

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
    const CXCursor written = strip_parens(member.front());
    const auto access = resolve_access(written);
    if (!access.has_value()) {
        return std::unexpected("the object of this call is not storage this implementation can name, such as a "
                               "local, a parameter or a member of one");
    }
    if (access->dereferenced) {
        return std::unexpected("a member function called through a pointer is not modeled: the object it designates "
                               "is a dereference, and a member call states no capability for it");
    }
    if (!access->symbolic_indices.empty()) {
        return std::unexpected("a member function called on an element selected at a term is not modeled");
    }
    const CXCursor object_record =
        clang_getCanonicalCursor(clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(written))));
    if (clang_equalCursors(object_record, callee_record) == 0) {
        return std::unexpected(base_class);
    }
    return CallObject{access->declaration, access->path, access->receiver};
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
    if (const std::optional<std::size_t> entry = find_local(locals, object.declaration, path)) {
        return read_place(locals, *entry, at);
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

// Whether two Clang types denote the same modeled value. Qualifiers are not
// part of a value, so a read of a `const` local is the value it holds; two
// spellings that Clang laid out identically are the same machine integer. A
// type C++L does not model is never "the same" as anything.
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

// Whether C++ performs arithmetic on this type only after promoting it to
// `int`. An update of such a local converts the promoted result back, which is
// a conversion C++L does not model.
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

std::string unmodeled_statement(const std::string& found) {
    return "only if/else, while and for loops, blocks, local declarations, assignments, and return statements are "
           "modeled; found " +
           found;
}

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

std::string statement_name(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_SwitchStmt:
            return "a 'switch' statement";
        case CXCursor_GotoStmt:
        case CXCursor_IndirectGotoStmt:
            return "a 'goto' statement";
        case CXCursor_LabelStmt:
            return "a label";
        case CXCursor_CXXTryStmt:
            return "a 'try' block (exceptions are not modeled)";
        case CXCursor_CXXThrowExpr:
            return "a 'throw' (exceptions are not modeled)";
        case CXCursor_NullStmt:
            return "an empty statement";
        case CXCursor_GCCAsmStmt:
        case CXCursor_MSAsmStmt:
            return "inline assembly";
        default:
            return "'" + take(clang_getCursorKindSpelling(kind)) + "'";
    }
}

// Observe one element of an array value at a symbolic index, with the bounds
// obligation the subscript owes (FOUNDATIONS.md 45, SPEC.md STORAGE-005).
//
// The extent is the resolved type's own component count, so it is available
// before any element has been observed. The obligation is the same
// `ElementBound` a tracked subscript owes, stated at the same index term this
// observation reads (ARCHITECTURE.md ARCH-ELEM-003, ARCH-ELEM-004).
Expr element_observation(Expr subject, CXCursor index_cursor, const Signature& signature, const Locals& locals,
                         unsigned depth, CXCursor cursor) {
    const std::size_t extent = subject.type.projections.size();
    if (extent == 0) {
        return unsupported_expression(cursor, "this subscript's array has no modeled extent");
    }
    Expr index = build_expression(index_cursor, signature, locals, depth + 1);
    if (index.type.kind != TypeKind::Int) {
        return unsupported_expression(cursor, "a symbolic array index must be an integer this implementation models");
    }
    // The extent enters the comparison at the index's own type, as it does for
    // a tracked subscript: a differing type is a conversion this implementation
    // does not model, and the obligation refuses it rather than inventing one.
    Expr count;
    count.type = index.type;
    count.location = index.location;
    count.node = IntLiteral{static_cast<std::int64_t>(extent)};

    Expr observed;
    observed.type = subject.type.projections.front();
    observed.location = presumed_location(clang_getCursorLocation(cursor));
    observed.node = Element{{std::move(subject), index}};

    Expr bound;
    bound.type = observed.type;
    bound.location = index.location;
    bound.node = ElementBound{{std::move(count)}, {std::move(index), std::move(observed)}};
    return bound;
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

std::expected<ElementRegion, std::string> element_region(CXCursor object, const Locals& locals,
                                                         const Signature& signature) {
    const std::vector<CXCursor>& parameters = signature.parameters;
    object = strip_parens(object);
    if (clang_getCursorKind(object) != CXCursor_DeclRefExpr) {
        return std::unexpected(
            std::string("a subscript of a container is modeled only on a local or parameter this body names directly"));
    }
    const CXCursor declaration = clang_getCursorReferenced(object);
    const std::string spelled = take(clang_getCursorSpelling(declaration));
    if (const auto binding = find_binding(locals, declaration)) {
        if (auto stale = stale_borrow(locals, *binding)) {
            return std::unexpected(std::move(*stale));
        }
        const std::size_t storage = locals[*binding].referent.value_or(*binding);
        const Local& accessed = locals[storage];
        if (!accessed.sequence.has_value()) {
            return std::unexpected("'" + spelled + "' is not a container this body tracks");
        }
        ElementRegion region;
        region.accessed = storage;
        const std::size_t root = accessed.sequence->views.value_or(storage);
        region.root = root;
        const Local* owner_entry = root < locals.size() ? &locals[root] : nullptr;
        if (owner_entry == nullptr || !owner_entry->sequence.has_value()) {
            return std::unexpected("the storage '" + spelled + "' views is not tracked by this body");
        }
        region.declaration = owner_entry->declaration;
        region.element = owner_entry->sequence->element;
        region.external = owner_entry->sequence->external_elements;
        region.family = accessed.sequence->kind;
        return region;
    }
    // A span parameter is not tracked: it is a value the call fixed, and its
    // elements are storage of the caller's (RFC 0020 §7).
    const auto at = std::ranges::find_if(
        parameters, [&](CXCursor candidate) { return clang_equalCursors(candidate, declaration); });
    if (at != parameters.end()) {
        const Type type = convert_type(clang_getCursorType(declaration));
        if (type.representation.kind == source::RepresentationKind::Span && type.representation.rejection.empty()) {
            auto element = sequence_element(declaration, clang_getCursorType(declaration), nullptr);
            if (!element) {
                return std::unexpected("span parameter '" + spelled + "': " + element.error());
            }
            ElementRegion region;
            region.declaration = declaration;
            // By its callable position, past a member function's implicit
            // object, as a capability names it (SPEC.md CLASS-008).
            region.parameter = signature.position(static_cast<std::size_t>(at - parameters.begin()));
            region.element = std::move(*element);
            region.external = true;
            region.family = source::RepresentationKind::Span;
            return region;
        }
    }
    return std::unexpected("'" + spelled + "' is not a container this body tracks");
}

// The length that bounds a subscript of the object `region` was reached
// through: that object's own, read at its current version (STDMODEL-012).
Expr region_length(const ElementRegion& region, const Locals& locals, CXCursor at) {
    Expr subject;
    if (region.accessed.has_value()) {
        const std::size_t accessed = *region.accessed;
        subject.type = locals[accessed].type;
        subject.location = presumed_location(clang_getCursorLocation(at));
        subject.node = PlaceRef{locals[accessed].version, place_of(locals, accessed)};
    } else if (region.parameter.has_value()) {
        // A span parameter, by its callable position; the region is rooted in
        // its declaration.
        subject.type = convert_type(clang_getCursorType(region.declaration));
        subject.location = presumed_location(clang_getCursorLocation(at));
        subject.node = ParameterRef{*region.parameter, take(clang_getCursorSpelling(region.declaration))};
    } else {
        return unsupported_expression(at, "the storage a subscript reaches has no length");
    }
    Expr length;
    length.type = subject.type.projections.empty() ? Type{} : subject.type.projections.front();
    length.location = subject.location;
    length.node = Projection{0, {std::move(subject)}};
    return length;
}

// The index term a subscript selects, stated at the modeled length type so it
// compares with the bound. A constant is that literal; anything else is the
// term Clang resolved, which must already be of the length type.
std::optional<Expr> element_index(const ResolvedAccess& access, const Type& length, const Signature& signature,
                                  const Locals& locals) {
    if (access.path.empty()) {
        return std::nullopt;
    }
    const PlaceStep& last = access.path.back();
    if (last.kind == PlaceStep::Kind::SymbolicElement) {
        if (access.symbolic_indices.empty()) {
            return std::nullopt;
        }
        return build_expression(access.symbolic_indices.back(), signature, locals, 0);
    }
    Expr literal;
    literal.type = length;
    literal.node = IntLiteral{static_cast<std::int64_t>(last.index)};
    return literal;
}

// The element place of `region` a subscript at `index` names, if this path has
// already formed it at the current generation.
std::optional<std::size_t> find_element(const Locals& locals, const ElementRegion& region,
                                        const std::vector<PlaceStep>& path, const Expr& index) {
    for (std::size_t position = locals.size(); position > 0; --position) {
        const Local& candidate = locals[position - 1];
        if (!candidate.symbolic || candidate.index_value.empty() ||
            clang_equalCursors(candidate.declaration, region.declaration) == 0 || candidate.path != path ||
            !same_term(candidate.index_value.front(), index) || !generation_current(locals, candidate)) {
            continue;
        }
        if (region.root.has_value() != candidate.formed_at.has_value() ||
            (region.root.has_value() && candidate.formed_at->root != *region.root)) {
            continue;
        }
        return position - 1;
    }
    return std::nullopt;
}

std::vector<CXCursor> parameters_of(CXCursor cursor);

// The identity of a library summary: its operation and the resolved type it is
// stated at (RFC 0020 §6). It names the summary, never a declaration, and is
// derived from Clang's USR of the specialization so two element types never
// share one. The mark is analysis only: the program keeps its own call, and
// nothing about it reaches the runtime translation unit (SPEC.md STDMODEL-022).
std::string library_symbol(source::LibraryCall library, const Type& container) {
    return "cppl-library:" + std::string(source::describe_model(library.container)) +
           "::" + std::string(source::describe(library.operation)) + "#" + container.representation.identity;
}

Expr library_call(source::LibraryCall library, const Type& container, std::string name, std::vector<Expr> arguments,
                  Type result, CXCursor at) {
    Call call;
    call.callee_usr = library_symbol(library, container);
    call.callee_name = std::move(name);
    call.arguments = std::move(arguments);
    call.library = library;
    Expr expression;
    expression.type = std::move(result);
    expression.location = presumed_location(clang_getCursorLocation(at));
    expression.node = std::move(call);
    return expression;
}

// The argument of `std::move(x)`, when `cursor` is that call: the function
// `move` Clang resolved in namespace `std`, never a spelling.
std::optional<CXCursor> moved_operand_of(CXCursor cursor) {
    cursor = strip_parens(cursor);
    if (clang_getCursorKind(cursor) != CXCursor_CallExpr || clang_Cursor_getNumArguments(cursor) != 1) {
        return std::nullopt;
    }
    const CXCursor callee = clang_getCursorReferenced(cursor);
    if (clang_getCursorKind(callee) != CXCursor_FunctionDecl || take(clang_getCursorSpelling(callee)) != "move") {
        return std::nullopt;
    }
    CXCursor parent = clang_getCursorSemanticParent(callee);
    while (clang_getCursorKind(parent) == CXCursor_Namespace && clang_Cursor_isInlineNamespace(parent) != 0) {
        parent = clang_getCursorSemanticParent(parent);
    }
    if (clang_getCursorKind(parent) != CXCursor_Namespace || take(clang_getCursorSpelling(parent)) != "std" ||
        clang_getCursorKind(clang_getCursorSemanticParent(parent)) != CXCursor_TranslationUnit) {
        return std::nullopt;
    }
    return clang_Cursor_getArgument(cursor, 0);
}

// The root of the tracked vector or string `object` names, directly or through
// a reference, when it names one (RFC 0020 §3). A span is not one: it owns no
// storage a view could be taken of.
std::optional<std::size_t> owning_root(CXCursor object, const Locals& locals) {
    object = strip_parens(object);
    if (clang_getCursorKind(object) != CXCursor_DeclRefExpr) {
        return std::nullopt;
    }
    const auto binding = find_binding(locals, clang_getCursorReferenced(object));
    if (!binding) {
        return std::nullopt;
    }
    const std::size_t storage = locals[*binding].referent.value_or(*binding);
    const Local& root = locals[storage];
    if (!root.sequence.has_value() || root.sequence->views.has_value() || !root.path.empty()) {
        return std::nullopt;
    }
    return storage;
}

// A span of a whole tracked container, formed where a call converts the
// container to the span a parameter takes (RFC 0020 §6, §7). Its length is the
// container's; its elements are the container's, which the caller owns or
// borrows by reference, so it is live for the call.
Expr view_argument(CXCursor source, const Type& span, const Locals& locals, CXCursor at) {
    if (!span.representation.rejection.empty()) {
        return unsupported_expression(at, "'" + span.spelling + "' is not modeled: " + span.representation.rejection);
    }
    const std::optional<std::size_t> root = owning_root(source, locals);
    if (!root) {
        return unsupported_expression(at, "a span is modeled only over a whole vector or string this body tracks");
    }
    Expr container;
    container.type = locals[*root].type;
    container.location = presumed_location(clang_getCursorLocation(source));
    container.node = PlaceRef{locals[*root].version, place_of(locals, *root)};
    return library_call({source::RepresentationKind::Span, source::LibraryOperation::ViewOf}, span,
                        "std::span(" + locals[*root].spelling + ")", {std::move(container)}, span, at);
}

bool is_mutator(const SequenceCall& call) {
    return call.name == "push_back" || call.name == "pop_back" || call.name == "clear" || call.name == "reserve" ||
           call.name == "append" || call.name == "operator+=" || call.name == "operator=";
}

// A member call of `std::array` or of a modeled sequence, read as a value
// (RFC 0020 §6). Observations are terms over the current version; element
// access reads the element place the statement formed; mutators and data
// pointers are refused here, because their meaning depends on where they
// stand, which the statement lowering decides.
Expr sequence_expression(const SequenceCall& call, CXCursor cursor, const Signature& signature, const Locals& locals,
                         unsigned depth) {
    using K = source::RepresentationKind;
    const std::string qualified = library_name(call.family, call.name);
    const Type resolved = convert_type(clang_getCursorType(cursor));
    if (call.constructor) {
        const std::vector<CXCursor> copied = parameters_of(call.method);
        // Copying a span copies the view, not the elements: the copy is the
        // same value, and designates the same storage while it lives
        // (SPEC.md J.2).
        if (call.family == K::Span && call.arguments.size() == 1 && copied.size() == 1 &&
            clang_equalCursors(clang_getTypeDeclaration(clang_getCanonicalType(
                                   clang_getPointeeType(clang_getCanonicalType(clang_getCursorType(copied.front()))))),
                               clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(cursor)))) != 0) {
            return build_expression(call.arguments.front(), signature, locals, depth + 1);
        }
        if (call.family == K::Span && call.arguments.size() == 1) {
            return view_argument(call.arguments.front(), resolved, locals, cursor);
        }
        // A copy of a tracked container, or the implicit move of a local a
        // return statement makes, is a container with the same value: the
        // length is all the abstract value holds, and both preserve it
        // (RFC 0020 §6). An explicit `std::move` is not this: it leaves its
        // operand unknown, so it is modeled only where it initializes a local.
        const std::vector<CXCursor> formals = parameters_of(call.method);
        if (source::is_sequence(call.family) && call.arguments.size() == 1 && formals.size() == 1 &&
            !moved_operand_of(call.arguments.front()).has_value()) {
            const CXType formal = clang_getCanonicalType(clang_getCursorType(formals.front()));
            const bool same_class =
                (formal.kind == CXType_LValueReference || formal.kind == CXType_RValueReference) &&
                clang_equalCursors(clang_getTypeDeclaration(clang_getCanonicalType(clang_getPointeeType(formal))),
                                   clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(cursor)))) != 0;
            if (same_class) {
                if (const std::optional<std::size_t> root = owning_root(call.arguments.front(), locals)) {
                    return read_place(locals, *root, cursor);
                }
            }
        }
        return unsupported_expression(cursor, "constructing '" + resolved.spelling +
                                                  "' here is not modeled; a container is constructed as the "
                                                  "initializer of a local (SPEC.md STDMODEL-013)");
    }
    const Type object = convert_type(clang_getCursorType(call.object), 0, ReferenceModel::Referent);
    if (!object.representation.rejection.empty()) {
        return unsupported_expression(cursor,
                                      "'" + object.spelling + "' is not modeled: " + object.representation.rejection);
    }
    const auto literal = [&](std::int64_t value, const Type& type) {
        Expr expression;
        expression.type = type;
        expression.location = presumed_location(clang_getCursorLocation(cursor));
        expression.node = IntLiteral{value};
        return expression;
    };
    if (call.family == K::StdArray) {
        const auto extent = static_cast<std::int64_t>(object.projections.size());
        if ((call.name == "size" || call.name == "max_size") && call.arguments.empty() &&
            resolved.kind == TypeKind::Int) {
            return literal(extent, resolved);
        }
        if (call.name == "empty" && call.arguments.empty() && resolved.kind == TypeKind::Bool) {
            return literal(extent == 0 ? 1 : 0, resolved);
        }
        if (call.name == "operator[]" && call.arguments.size() == 1) {
            // A tracked array's element is its own place; an array held as a
            // value is observed at the index (FOUNDATIONS.md 45).
            if (const auto element = tracked_place(cursor, locals, signature)) {
                return read_place(locals, *element, cursor);
            }
            // An array a reference designates is caller storage another
            // reference may write while the body runs, so it is not read as the
            // value it had on entry (TRUST.md TCB-MEM-005).
            if (const CXCursor named = strip_parens(call.object);
                clang_getCursorKind(named) == CXCursor_DeclRefExpr &&
                source::aliases_storage(passing_of(clang_getCursorType(clang_getCursorReferenced(named))))) {
                return unsupported_expression(cursor, "an element of the std::array a reference designates is not "
                                                      "modeled; take the array by value (SPEC.md STDMODEL-011)");
            }
            Expr subject = build_expression(call.object, signature, locals, depth + 1);
            if (subject.type.representation.kind != K::StdArray) {
                return unsupported_expression(cursor, "this subscript's array is not a value or storage this body "
                                                      "models");
            }
            if (const auto index = constant_index_of(call.arguments.front())) {
                if (*index >= subject.type.projections.size()) {
                    return unsupported_expression(cursor,
                                                  "proof array index must be a constant within the resolved extent");
                }
                Expr projected;
                projected.type = subject.type.projections[*index];
                projected.location = presumed_location(clang_getCursorLocation(cursor));
                projected.node = Projection{*index, {std::move(subject)}};
                return projected;
            }
            return element_observation(std::move(subject), call.arguments.front(), signature, locals, depth, cursor);
        }
        return unsupported_expression(cursor, "'" + qualified +
                                                  "' is not a modeled operation of std::array; size(), empty() and "
                                                  "operator[] are (SPEC.md STDMODEL-011)");
    }
    const bool length = call.name == "size" || (call.family == K::String && call.name == "length");
    if ((length || call.name == "empty") && call.arguments.empty()) {
        Expr subject = build_expression(call.object, signature, locals, depth + 1);
        if (std::holds_alternative<Unsupported>(subject.node)) {
            return subject;
        }
        if (subject.type.representation.kind != call.family || subject.type.projections.size() != 1) {
            return unsupported_expression(cursor, "the length of this container is not a value this body models");
        }
        const Type size = subject.type.projections.front();
        Expr observed;
        observed.type = size;
        observed.location = presumed_location(clang_getCursorLocation(cursor));
        observed.node = Projection{0, {std::move(subject)}};
        if (length) {
            // The modeled length type is what Clang says `size()` returns, or
            // the observation is refused rather than converted.
            if (!same_modeled_value(resolved, size)) {
                return unsupported_expression(cursor, "'" + qualified + "' returns '" + resolved.spelling +
                                                          "', which is not the modeled length type");
            }
            return observed;
        }
        if (resolved.kind != TypeKind::Bool) {
            return unsupported_expression(cursor, "'" + qualified + "' does not return bool");
        }
        Expr empty;
        empty.type = resolved;
        empty.location = observed.location;
        empty.node = Binary{BinaryOp::Equal, {std::move(observed), literal(0, size)}};
        return empty;
    }
    if (call.name == "operator[]" && call.arguments.size() == 1) {
        auto region = element_region(call.object, locals, signature);
        if (!region) {
            return unsupported_expression(cursor, region.error());
        }
        const auto access = resolve_access(cursor);
        const Type size = region_length(*region, locals, cursor).type;
        if (!access) {
            return unsupported_expression(cursor, "this subscript's index could not be resolved");
        }
        const std::optional<Expr> index = element_index(*access, size, signature, locals);
        if (!index) {
            return unsupported_expression(cursor, "this subscript's index could not be resolved");
        }
        if (const auto element = find_element(locals, *region, access->path, *index)) {
            return read_place(locals, *element, cursor);
        }
        return unsupported_expression(cursor, "this element access is not one the statement holding it formed");
    }
    if (call.name == "data") {
        return unsupported_expression(cursor, "'" + qualified +
                                                  "' is modeled only as the argument of a verified call whose "
                                                  "parameter holds a memory capability (SPEC.md STDMODEL-017)");
    }
    if (is_mutator(call)) {
        return unsupported_expression(cursor, "'" + qualified +
                                                  "' is modeled only as a statement of its own, where the storage it "
                                                  "may replace is followed (SPEC.md STDMODEL-013)");
    }
    return unsupported_expression(cursor, "'" + qualified + "' is not a modeled operation of " +
                                              std::string(source::describe_model(call.family)) +
                                              " (SPEC.md STDMODEL-019)");
}

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
            return unsupported_expression(cursor, "the implicit object is not one modeled value; a member function "
                                                  "reads and writes it member by member (SPEC.md CLASS-008)");
        }
        if (const auto pointee = tracked_place(cursor, locals, signature)) {
            return read_place(locals, *pointee, cursor);
        }
    }
    // A member of the implicit object, however it is written: `this->x`,
    // `(*this).x`, or `x` alone (SPEC.md CLASS-008).
    if (kind == CXCursor_MemberRefExpr || kind == CXCursor_ArraySubscriptExpr) {
        if (const auto access = resolve_access(cursor); access && access->receiver) {
            return read_receiver(*access, cursor, signature, locals);
        }
    }
    if (kind == CXCursor_MemberRefExpr) {
        const auto field = clang_getCursorReferenced(cursor);
        const auto children = children_of(cursor);
        if (clang_getCursorKind(field) == CXCursor_FieldDecl && children.size() == 1) {
            // A member of a tracked object is its own place, so it is read at
            // its own current version rather than projected out of a value of
            // the whole object: a later write to a sibling must not disturb it,
            // and a write to this member must (SPEC.md 12.10).
            if (const auto member = tracked_place(cursor, locals, signature)) {
                return read_place(locals, *member, cursor);
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
        Call call;
        call.callee_usr = take(clang_getCursorUSR(referenced));
        call.callee_name = qualified_name_of(referenced);

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

std::vector<CXCursor> parameters_of(CXCursor cursor) {
    std::vector<CXCursor> parameters;
    // Negative for a cursor that is not a function; there are then no arguments.
    const int count = clang_Cursor_getNumArguments(cursor);
    if (count <= 0) {
        return parameters;
    }
    parameters.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        parameters.push_back(clang_Cursor_getArgument(cursor, static_cast<unsigned>(index)));
    }
    return parameters;
}

std::size_t return_paths(const Expr& expression) {
    if (const auto* branch = std::get_if<Conditional>(&expression.node)) {
        return return_paths(branch->operands[1]) + return_paths(branch->operands[2]);
    }
    if (const auto* bound = std::get_if<PlaceVersion>(&expression.node)) {
        return return_paths(bound->operands[1]);
    }
    if (const auto* loop = std::get_if<Loop>(&expression.node)) {
        return return_paths(loop->operands.back());
    }
    if (const auto* unknown = std::get_if<UnknownVersion>(&expression.node); unknown && unknown->operands.size() == 1)
        return return_paths(unknown->operands.front());
    if (const auto* region = std::get_if<UnsafeRegion>(&expression.node); region && region->operands.size() == 1)
        return return_paths(region->operands.front());
    return 1;
}

std::size_t file_offset(CXSourceLocation location) {
    unsigned offset = 0;
    clang_getFileLocation(location, nullptr, nullptr, nullptr, &offset);
    return static_cast<std::size_t>(offset);
}

// The parts of a `for` header. libclang omits an empty part instead of marking
// it, so each part is placed by where it starts relative to the header's two
// top-level semicolons.
struct ForParts {
    std::optional<CXCursor> initialization;
    std::optional<CXCursor> condition;
    std::optional<CXCursor> increment;
    CXCursor body = clang_getNullCursor();
};

std::optional<ForParts> for_parts(CXCursor statement) {
    const std::vector<CXCursor> children = children_of(statement);
    if (children.empty()) {
        return std::nullopt;
    }
    CXTranslationUnit unit = clang_Cursor_getTranslationUnit(statement);
    CXToken* tokens = nullptr;
    unsigned count = 0;
    clang_tokenize(unit, clang_getCursorExtent(statement), &tokens, &count);
    struct Release {
        CXTranslationUnit unit;
        CXToken* tokens;
        unsigned count;
        ~Release() {
            if (tokens != nullptr) {
                clang_disposeTokens(unit, tokens, count);
            }
        }
    } release{unit, tokens, count};

    std::vector<std::size_t> separators;
    int nesting = 0;
    for (unsigned index = 0; index < count; ++index) {
        if (clang_getTokenKind(tokens[index]) != CXToken_Punctuation) {
            continue;
        }
        const std::string spelling = take(clang_getTokenSpelling(unit, tokens[index]));
        if (spelling == "(" || spelling == "[" || spelling == "{") {
            ++nesting;
        } else if (spelling == ")" || spelling == "]" || spelling == "}") {
            if (--nesting == 0) {
                break;
            }
        } else if (spelling == ";" && nesting == 1) {
            separators.push_back(file_offset(clang_getTokenLocation(unit, tokens[index])));
        }
    }
    if (separators.size() != 2) {
        return std::nullopt;
    }

    ForParts parts;
    parts.body = children.back();
    for (std::size_t index = 0; index + 1 < children.size(); ++index) {
        const std::size_t start = file_offset(clang_getRangeStart(clang_getCursorExtent(children[index])));
        std::optional<CXCursor>& part = start < separators[0]   ? parts.initialization
                                        : start < separators[1] ? parts.condition
                                                                : parts.increment;
        if (part.has_value()) {
            return std::nullopt;
        }
        part = children[index];
    }
    return parts;
}

// Marks each local in `locals` that the statement or expression writes by
// assignment, compound assignment, increment or decrement. Any other way of
// writing a local is refused when the body is lowered, and a local this misses
// is caught at the end of every iteration, so the scan only has to be complete
// for the writes the lowering accepts.
struct WriteScan {
    const Locals* locals;
    std::vector<bool>* written;
};

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

std::optional<std::size_t> written_storage(CXCursor declaration, const Locals& locals, unsigned depth = 0) {
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
    for (std::size_t target = 0; target < locals.size(); ++target) {
        if (!written[target] || !locals[target].external)
            continue;
        for (std::size_t other = 0; other < locals.size(); ++other)
            if (locals[other].external && !locals[other].referent &&
                (same_modeled_value(locals[target].type, locals[other].type) ||
                 locals[other].type.kind == TypeKind::Value) &&
                !distinct_members(locals[target], locals[other]))
                written[other] = true;
    }
}

// Whether control leaves the function rather than reaching what follows. It
// decides reachability only; what each statement means is decided by the
// lowering below.
bool terminates(CXCursor statement, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return false;
    }
    const CXCursorKind kind = clang_getCursorKind(statement);
    if (kind == CXCursor_ReturnStmt || kind == CXCursor_BreakStmt || kind == CXCursor_ContinueStmt) {
        return true;
    }
    if (kind == CXCursor_CompoundStmt) {
        const std::vector<CXCursor> nested = children_of(statement);
        return std::ranges::any_of(nested, [depth](CXCursor child) { return terminates(child, depth + 1); });
    }
    if (kind == CXCursor_IfStmt) {
        const std::vector<CXCursor> parts = children_of(statement);
        return parts.size() == 3 && terminates(parts[1], depth + 1) && terminates(parts[2], depth + 1);
    }
    return false;
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
std::vector<CXCursor> unsafe_blocks_in(CXCursor root, const std::string& prefix, unsigned depth = 0) {
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

// Whether code in `root` may change what `declaration` itself holds, now or
// later: by writing it or a member or element of it, by taking the address of
// any of those, by binding a reference to one that is not const, by calling a
// member function on it, or by capturing it in a lambda. Reading it, passing it
// by value and reaching what it points to leave it as it was.
bool may_rebind(CXCursor root, CXCursor declaration, unsigned depth = 0) {
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
    return std::nullopt;
}

// What a verified body declares as ghost state, and every error in how it
// declares or uses it (SPEC.md GHOST-001, GHOST-002).
//
// Ghost state leaves the program before it runs, so nothing that runs may name
// it: not a returned value, a branch, an index, an argument, an initializer or
// a write. The only places that may are what leaves with it, the initializer of
// another ghost declaration and the specification expressions the projector
// declared under its own prefix (loop clauses, claim arguments, split subjects).
// Every other reference is an error where it stands, whatever path it is on.
class GhostScan {
  public:
    explicit GhostScan(std::string prefix) : prefix_(std::move(prefix)) {}

    void run(CXCursor body) {
        declare(body, 0);
        if (!ghosts_.empty()) {
            leaks(body, false, 0);
        }
    }

    std::vector<Function::GhostError> errors;
    std::vector<Function::GhostCall> calls;

  private:
    void error(std::string message, std::string note, CXCursor at) {
        errors.push_back(
            Function::GhostError{std::move(message), std::move(note), presumed_location(clang_getCursorLocation(at))});
    }

    [[nodiscard]] bool is_ghost(CXCursor declaration) const {
        return std::ranges::any_of(ghosts_,
                                   [&](CXCursor ghost) { return clang_equalCursors(ghost, declaration) != 0; });
    }

    void declare(CXCursor cursor, unsigned depth) {
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

    void admit(CXCursor declaration) {
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

    void collect_calls(CXCursor cursor, const std::string& ghost, unsigned depth) {
        if (depth > kMaxExpressionDepth) {
            return;
        }
        if (clang_getCursorKind(cursor) == CXCursor_CallExpr) {
            const CXCursor callee = clang_getCursorReferenced(cursor);
            calls.push_back(Function::GhostCall{
                clang_Cursor_isNull(callee) != 0 ? std::string{} : take(clang_getCursorUSR(callee)),
                take(clang_getCursorSpelling(cursor)), ghost, presumed_location(clang_getCursorLocation(cursor))});
        }
        for (const CXCursor child : children_of(cursor)) {
            collect_calls(child, ghost, depth + 1);
        }
    }

    void leaks(CXCursor cursor, bool proof_only, unsigned depth) {
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

    std::string prefix_;
    std::vector<CXCursor> ghosts_;
};

// What remains to be executed after the statement being lowered: the rest of
// its block, and whatever follows the blocks enclosing it. A branch lowers this
// continuation once per arm, under the versions that arm established, which is
// what makes a local's value path-sensitive without any merge rule.
struct LoopFrame;
struct LoopHeader;

struct Continuation {
    const Continuation* outer = nullptr;
    const std::vector<CXCursor>* statements = nullptr;
    std::size_t index = 0;

    // In place of statements: the end of one iteration of a loop, before or
    // after its increment, or a `for` loop whose initialization is done.
    const LoopFrame* iteration = nullptr;
    bool after_increment = false;
    const LoopHeader* header = nullptr;
};

// A loop about to be entered.
struct LoopHeader {
    CXCursor statement = clang_getNullCursor();
    CXCursor condition = clang_getNullCursor(); // null for a `for` without one
    CXCursor body = clang_getNullCursor();
    std::optional<CXCursor> increment;
    const Continuation* exit = nullptr; // what follows the loop
    // A `do` loop: the body runs first, and the condition decides at the end
    // of each iteration whether another begins (SPEC.md LOOP-003).
    bool condition_last = false;
};

// A loop whose body is being lowered.
struct LoopFrame {
    std::uint32_t id = 0;
    CXCursor statement = clang_getNullCursor();
    Locals head;                      // the locals at the head, each carried one at its head version
    std::vector<std::size_t> carried; // positions in `head` that the loop writes
    std::optional<CXCursor> increment;
    const Continuation* exit = nullptr;
    std::size_t frames_outside = 0; // the enclosing loops, for a `break` into what follows
    CXCursor condition = clang_getNullCursor();
    bool condition_last = false;
};

// A memory capability the contract of the body being lowered states, resolved
// to the parameter whose pointee it describes (SPEC.md 12.10).
//
// This is what makes a dereference legal inside the body. It is not evidence
// the body produces: the caller owes it at the call, and here it is a
// hypothesis with a stated origin.
struct StatedCapability {
    std::uint32_t parameter = 0;
    Capability::Kind kind = Capability::Kind::Readable;

    // The element count of the sized form, `readable(p, n)`, as the term the
    // contract stated. Empty for the one-object abbreviation `readable(p)`.
    //
    // The term is kept rather than a flag: a subscript through this capability
    // owes `index < n`, and `n` is a value no literal is available for
    // (SPEC.md 12.10, VERIFIED-038).
    std::vector<Expr> extent;
};

// Lowers a resolved function body into the value it returns.
//
// Statements are taken in program order, threading the logical version of each
// local. A declaration or an assignment binds the next version and the rest of
// the body is lowered under it; a read of a local denotes the version current
// where the read stands. Nothing here rewrites the program: the versions are a
// model of the body Clang resolved (SPEC.md 12.8).
struct BodyLowering {
    // The body's signature, and the parameters Clang resolved for it, which are
    // its written parameters. A member function's implicit object is not among
    // them: its leaves are tracked as storage rooted in its class (SPEC.md
    // CLASS-008).
    const Signature& signature;
    const std::vector<CXCursor>& parameters;
    Type result_type;
    // The projector's generated prefix, which every declaration it puts in a
    // body carries: loop clauses, contradiction blocks, instantiation markers.
    std::string invariant_prefix;
    const std::vector<Selection::Refinement>* refinements = nullptr;
    std::uint32_t next_version = 0;
    std::uint32_t next_loop = 0;
    std::vector<const LoopFrame*> frames;
    std::vector<std::string> consumed_invariants;
    std::vector<std::string> consumed_contradictions;
    std::vector<Function::SplitSubject> consumed_splits;
    std::vector<std::string> consumed_unsafe;

    // Where the path being lowered passed through an unsafe block, if it has.
    // From there on the path holds none of the capabilities its contract stated:
    // the block may have ended a lifetime, released storage or moved a pointer's
    // target, and nothing checked that it did not (SPEC.md UNSAFE-003,
    // ARCHITECTURE.md ARCH-UNSAFE-002). A loop that holds an unsafe block is
    // such a point for its every iteration and for what follows it.
    std::optional<source::SourceLocation> revoked_by;
    std::string rejection;
    bool executable_state = true;
    source::SourceLocation completion_location = {};

    // Locals whose address is taken somewhere in this body, by Clang's
    // resolution of `&x`. A local not in this set cannot be the pointee of any
    // pointer, so a write through a pointer cannot reach it. Escape is
    // permanent and computed for the whole body, never per program point: a
    // pointer formed on one path may be written through on another.
    std::unordered_set<unsigned> escaped;

    // Locals some unmodeled write could reach, which is a stricter question
    // than `escaped` answers. See `unconfined_locals`.
    std::unordered_set<unsigned> unconfined;

    // Dereference places formed while lowering the statement in hand, awaiting
    // the binding that gives each one an entry value.
    //
    // A pointee is caller storage: this body did not write it, so its value is
    // opaque and inherits no fact, exactly as a havocked place does. Binding it
    // is what makes a read of it well formed, and the binding must wrap the
    // continuation, which only the statement lowering can do.
    // The entries themselves rather than indices into a `Locals`: each
    // statement form lowers over its own copy of the locals, so an index would
    // not survive back to where the binding is emitted.
    std::vector<Local> formed_derefs;

    // Whether the value bound for a newly formed place is known to inhabit the
    // place's declared type, so the walk may state that type's refinement of it
    // (SPEC.md REFINE-060).
    //
    // The rule needs closed accounting for every write that may reach the
    // place: validity for the version it entered the modeled state with, and
    // the same predicate charged at every write since. Where that holds, the
    // element's current version holds a value of its type even though which
    // element it is stays undecided.
    //
    // What follows is where *this* implementation has that accounting, not the
    // limit of where it could be had. Indirection does not disqualify a place
    // in principle; it disqualifies it here because nothing closes the
    // accounting behind a pointer, whose declared pointee type is erased and so
    // is no evidence at all about what the pointee holds. The same goes for
    // storage a reference parameter designates, which the caller may write
    // through another reference, and for a local whose address escaped, which a
    // write this body never modeled can reach. Widening any of these means
    // establishing the accounting first, never relaxing the test.
    [[nodiscard]] bool confined_element(const Local& entry) const {
        return entry.symbolic && !entry.is_deref() && !entry.external &&
               !unconfined.contains(clang_hashCursor(entry.declaration));
    }

    // Wrap `body` in an opaque binding for each dereference place formed while
    // the statement was lowered, outermost first so each version is bound
    // before anything reads it.
    Expr bind_formed_derefs(Expr body, CXCursor at) {
        for (const Local& entry : std::ranges::reverse_view(formed_derefs)) {
            Locals one{entry};
            body = unknown(one, 0, std::move(body), at, confined_element(entry));
            // A symbolic element owes `index < extent` where it was formed. The
            // bound wraps the binding, so the obligation stands whether or not
            // the element's value is ever used.
            if (entry.symbolic && !entry.index_value.empty() && !entry.extent.empty()) {
                Expr bound;
                bound.type = body.type;
                bound.location = entry.index_value.front().location;
                bound.node = ElementBound{entry.extent, {entry.index_value.front(), std::move(body)}};
                body = std::move(bound);
            }
        }
        formed_derefs.clear();
        return body;
    }

    // The memory capabilities this body may rely on, by the parameter index of
    // the pointer each one names. These come from the contract's `expects`
    // clauses and from nothing else: a capability is established by a proven
    // obligation or a recorded trusted boundary, never because an access needed
    // it (AGENTS.md storage invariants, SPEC.md VERIFIED-043).
    const std::vector<StatedCapability>* capabilities = nullptr;

    // Whether the contract grants `kind` on the pointee of the pointer held in
    // `parameter`. `writable` does not entail `readable` and `readable` does
    // not entail `writable`: an output buffer may be written and not read
    // (RFC 0014 §3).
    [[nodiscard]] const StatedCapability* granted_capability(std::uint32_t parameter, Capability::Kind kind) const {
        if (capabilities == nullptr || revoked_by.has_value()) {
            return nullptr;
        }
        const auto at = std::ranges::find_if(*capabilities, [&](const StatedCapability& stated) {
            return stated.parameter == parameter && stated.kind == kind;
        });
        return at == capabilities->end() ? nullptr : &*at;
    }

    [[nodiscard]] bool granted(std::uint32_t parameter, Capability::Kind kind) const {
        return granted_capability(parameter, kind) != nullptr;
    }

    // Why dereferencing pointer parameter `spelling` is refused for want of the
    // capability `required`.
    [[nodiscard]] std::string capability_refusal(const std::string& spelling, Capability::Kind required) const {
        const bool writing = required == Capability::Kind::Writable;
        return std::string(writing ? "writing through '" : "reading '") + spelling + "' requires '" +
               (writing ? "writable(" : "readable(") + spelling + ")', " +
               (revoked_by.has_value()
                    ? "which no longer holds after the unsafe block at " + revoked_by->file + ":" +
                          std::to_string(revoked_by->line) + ": what that block did to the storage was not checked"
                    : "which was not established; 'p != nullptr' does not imply it");
    }

    // Whether a normal return states the storage the caller can see afterwards:
    // a void function's, and one taking a parameter by reference. A member
    // function's implicit object is such storage whenever it has a leaf, since
    // each leaf is a reference parameter (SPEC.md CLASS-008).
    bool has_post_state() const {
        return executable_state && (result_type.kind == TypeKind::Void || signature.leaves() != 0 ||
                                    std::ranges::any_of(parameters, [](CXCursor parameter) {
                                        return source::aliases_storage(passing_of(clang_getCursorType(parameter)));
                                    }));
    }

    Expr completed(Expr value, const Locals& locals, CXCursor at) {
        if (!has_post_state())
            return value;
        ReturnState state;
        state.operands.push_back(std::move(value));
        // The implicit object's leaves first, each at the version current where
        // the function returns: the post-state its postcondition describes
        // (SPEC.md CONTRACT-009). Every leaf is tracked from entry.
        if (signature.receiver.has_value()) {
            for (const ReceiverLeaf& leaf : signature.receiver->leaves) {
                const auto local = find_local(locals, signature.receiver->record, leaf.path);
                if (!local) {
                    return unsupported_expression(at, "the implicit object's '" + leaf.spelling +
                                                          "' is not tracked where the function returns");
                }
                state.operands.push_back(read_place(locals, *local, at));
            }
        }
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const auto local = find_local(locals, parameters[index]);
            if (local && source::aliases_storage(passing_of(clang_getCursorType(parameters[index])))) {
                state.operands.push_back(read_place(locals, *local, at));
            } else {
                Expr input;
                input.type = convert_type(clang_getCursorType(parameters[index]), 0, ReferenceModel::Referent);
                input.location = presumed_location(clang_getCursorLocation(at));
                input.node = ParameterRef{signature.position(index), take(clang_getCursorSpelling(parameters[index]))};
                state.operands.push_back(std::move(input));
            }
        }
        Expr result;
        result.type = result_type;
        result.location =
            clang_Cursor_isNull(at) ? completion_location : presumed_location(clang_getCursorLocation(at));
        result.node = std::move(state);
        return result;
    }

    Expr void_value(CXCursor at) const {
        Expr value;
        value.type = result_type;
        value.location = clang_Cursor_isNull(at) ? completion_location : presumed_location(clang_getCursorLocation(at));
        value.node = IntLiteral{0};
        return value;
    }

    // Resolve an access to the entry holding the storage it names, forming a
    // dereference place when it goes through a pointer (RFC 0014 §1, §17
    // steps 5-6).
    //
    // This is the single point where a pointer becomes a place, so the
    // capability obligation is owed here and cannot be bypassed by choosing a
    // different syntax: `*p`, `p->m` and `p[i]` all arrive here. The capability
    // must already be in scope; nothing about the pointer's value establishes
    // it, and it is never assumed because the access needed it (SPEC.md
    // VERIFIED-037, VERIFIED-043).
    //
    // `required` is the capability the access needs: reading requires
    // `readable`, writing requires `writable`, and neither entails the other.
    std::optional<std::size_t> resolve_storage(CXCursor cursor, Locals& state, Capability::Kind required) {
        const auto access = resolve_access(cursor);
        if (!access) {
            return std::nullopt;
        }
        const auto declaration = access->declaration;
        if (!access->dereferenced) {
            return find_local(state, declaration, access->path);
        }
        // The pointer must be a parameter the contract can name, because a
        // capability is stated about a parameter. A pointer that is a local has
        // no stated capability and no way to earn one yet, so it fails closed.
        //
        // The pointer's own storage need not be tracked: what is tracked is the
        // pointee place. A pointer parameter is not a modeled value here, and a
        // write to the pointer itself is refused elsewhere, so the version that
        // identifies the pointee is the pointer's initial one.
        const auto at = std::ranges::find_if(
            parameters, [&](CXCursor candidate) { return clang_equalCursors(candidate, declaration) != 0; });
        const auto pointer = find_local(state, declaration);
        const std::size_t root = pointer.value_or(
            at == parameters.end() ? std::size_t{0} : static_cast<std::size_t>(at - parameters.begin()));
        const std::uint32_t version = pointer ? state[*pointer].version : 0;
        // A symbolic subscript is identified by its index term as well as by its
        // path, so `p[i]` and `p[j]` are two places. The term is read here, at
        // the versions current before this access forms anything, which is the
        // same state it would be read in below.
        std::optional<Expr> selected_index;
        if (!access->path.empty() && access->path.back().kind == PlaceStep::Kind::SymbolicElement &&
            !access->symbolic_indices.empty()) {
            selected_index = build_expression(access->symbolic_indices.back(), signature, state, 0);
        }
        if (auto existing = find_deref(state, root, version, access->path, selected_index ? &*selected_index : nullptr);
            existing.has_value()) {
            // A place formed earlier is reached again only under the capability
            // this access needs, still held here (SPEC.md VERIFIED-038): a write
            // needs `writable` even where a read formed the place, a read needs
            // `readable` even where a write formed it, and no capability
            // survives an unsafe block (VERIFIED-043). The capability names the
            // pointer by its callable position (SPEC.md CLASS-008).
            const bool held = at != parameters.end() &&
                              granted(signature.position(static_cast<std::size_t>(at - parameters.begin())), required);
            if (!held) {
                rejection = capability_refusal(take(clang_getCursorSpelling(declaration)), required);
                return std::nullopt;
            }
            return existing;
        }
        if (at == parameters.end()) {
            rejection = "dereferencing '" + take(clang_getCursorSpelling(declaration)) +
                        "' requires a memory capability, and only a pointer parameter named by an expects clause "
                        "can carry one";
            return std::nullopt;
        }
        // A capability names the pointer by its callable position, which is
        // past a member function's implicit object (SPEC.md CLASS-008).
        const auto index = signature.position(static_cast<std::size_t>(at - parameters.begin()));
        if (!granted(index, required)) {
            rejection = capability_refusal(take(clang_getCursorSpelling(declaration)), required);
            return std::nullopt;
        }
        // A capability permits reaching the pointer's storage; it does not say
        // which element of that storage a subscript names. The two are separate
        // obligations and stay separate: the capability is tracked as a context
        // hypothesis, while `index < extent` is a proposition about values that
        // the kernel proves (RFC 0014 §10, §17 step 7, SPEC.md VERIFIED-038).
        //
        // Only the sized form states an extent. `readable(p)` describes one
        // object, so it reaches no element beyond the first and there is no
        // bound to compare against; the access fails closed rather than
        // treating an unstated extent as an unbounded one (VERIFIED-043).
        const bool subscripted = std::ranges::any_of(access->path, [](const PlaceStep& step) {
            return step.kind == PlaceStep::Kind::Element || step.kind == PlaceStep::Kind::SymbolicElement;
        });
        const StatedCapability* stated = granted_capability(index, required);
        std::vector<Expr> element_extent;
        if (subscripted) {
            if (stated == nullptr || stated->extent.empty()) {
                rejection = "subscripting '" + take(clang_getCursorSpelling(declaration)) + "' requires '" +
                            (required == Capability::Kind::Writable ? "writable(" : "readable(") +
                            take(clang_getCursorSpelling(declaration)) +
                            ", n)' to state the extent its index must lie within; the one-object form bounds no "
                            "element";
                return std::nullopt;
            }
            element_extent = stated->extent;
        }
        // The pointee type is what the pointer points to, with its sugar kept
        // so a refinement named on the pointee is still known.
        const CXType pointee = clang_getPointeeType(clang_getCursorType(declaration));
        Type type = convert_type(pointee, 0, ReferenceModel::Opaque, refinements);
        if (type.kind == TypeKind::Unsupported) {
            rejection = "the pointee of '" + take(clang_getCursorSpelling(declaration)) + "' is not modeled";
            return std::nullopt;
        }
        // A refinement on the pointee is verification-level identity Clang
        // canonicalizes away, so it is recovered from the written type. Without
        // this a write through `Positive*` would owe nothing (SPEC.md 17.3).
        if (refinements != nullptr) {
            auto resolved = refinements_of(declaration, pointee, *refinements);
            if (!resolved) {
                rejection =
                    "the pointee of '" + take(clang_getCursorSpelling(declaration)) + "' has " + resolved.error();
                return std::nullopt;
            }
            type.refinements = std::move(*resolved);
        }
        Local entry;
        entry.declaration = declaration;
        entry.version = next_version++;
        entry.type = std::move(type);
        entry.path = access->path;
        entry.pointer = root;
        entry.pointer_version = version;
        entry.spelling = "*" + take(clang_getCursorSpelling(declaration));
        // A subscript through a capability owes `index < n` against the extent
        // the contract stated, whether the index is a term or a constant. The
        // index is lowered here, where the place is formed, so it denotes the
        // versions current at the access.
        if (subscripted) {
            const PlaceStep& last = access->path.back();
            Expr selected;
            if (last.kind == PlaceStep::Kind::SymbolicElement) {
                if (!selected_index) {
                    rejection = "this subscript has no index expression to bound";
                    return std::nullopt;
                }
                selected = std::move(*selected_index);
            } else {
                // A constant index states the same obligation: `a[999]` owes
                // `999 < n` exactly as `a[i]` owes `i < n`. Nothing about a
                // literal makes it within the extent.
                selected.type = element_extent.front().type;
                selected.location = element_extent.front().location;
                selected.node = IntLiteral{static_cast<std::int64_t>(last.index)};
            }
            entry.symbolic = true;
            entry.index_value.push_back(std::move(selected));
            entry.extent = std::move(element_extent);
        }
        state.push_back(std::move(entry));
        return state.size() - 1;
    }

    // The resolved type of the storage `prefix` designates within `declaration`.
    //
    // This answers "what indexed structure does this object have", which is a
    // question about its C++ type and not about which of its elements the proof
    // has met so far. The place answers "which object" separately
    // (ARCHITECTURE.md ARCH-ELEM-004).
    Type declared_place_type(CXCursor declaration, const std::vector<PlaceStep>& prefix, const Locals& state) const {
        // A tracked entry for the whole object is preferred: it already carries
        // the type the declaration was modeled with, including a reference
        // parameter's referent type.
        for (const Local& candidate : state) {
            if (clang_equalCursors(candidate.declaration, declaration) != 0 && !candidate.symbolic &&
                candidate.path.empty()) {
                return walk_components(candidate.type, prefix);
            }
        }
        if (clang_Cursor_isNull(declaration) != 0) {
            return {};
        }
        return walk_components(convert_type(reference_value_type(clang_getCursorType(declaration))), prefix);
    }

    // The type each step of `path` selects, by the resolved component order the
    // representation already records.
    static Type walk_components(Type current, const std::vector<PlaceStep>& path) {
        for (const PlaceStep& step : path) {
            if (step.kind == PlaceStep::Kind::SymbolicElement || step.index >= current.projections.size()) {
                return {};
            }
            current = current.projections[step.index];
        }
        return current;
    }

    // Form the place a symbolic subscript names, with the bounds obligation it
    // owes (RFC 0014 §17 step 7).
    //
    // The element is undecided, so it gets its own place and an opaque value:
    // nothing here decides which element it is. The bounds obligation is a
    // proposition about values -- `index < extent` -- so it is proved by the
    // kernel rather than tracked as a capability (RFC 0014 §10).
    std::optional<std::size_t> resolve_symbolic_element(Locals& state, const ResolvedAccess& access) {
        const auto declaration = access.declaration;
        if (access.symbolic_indices.empty()) {
            rejection = "this subscript has no index expression to bound";
            return std::nullopt;
        }
        // The index term is read before anything is formed, so an existing place
        // is recognized by the value its index has here rather than by the path
        // alone, which records only that some step was symbolic.
        Expr selected = build_expression(access.symbolic_indices.front(), signature, state, 0);
        if (const auto existing = find_symbolic(state, declaration, access.path, selected); existing.has_value()) {
            return existing;
        }
        // An array local is tracked as one entry per element, so the extent is
        // how many element entries this array has and the element type is
        // theirs. Both come from Clang's resolved layout rather than a separate
        // claim (RFC 0014 §2).
        //
        // The prefix is the path up to the symbolic step; the elements of the
        // array being indexed are the entries sharing it with one more step.
        std::vector<PlaceStep> prefix(access.path.begin(), access.path.end() - 1);
        std::uint32_t extent = 0;
        const Type* element = nullptr;
        for (const Local& candidate : state) {
            if (clang_equalCursors(candidate.declaration, declaration) == 0 ||
                candidate.path.size() != prefix.size() + 1 || candidate.symbolic ||
                !std::equal(prefix.begin(), prefix.end(), candidate.path.begin()) ||
                candidate.path.back().kind != PlaceStep::Kind::Element) {
                continue;
            }
            extent = std::max(extent, candidate.path.back().index + 1);
            element = &candidate.type;
        }
        // The extent belongs to the array's resolved type, so it is known
        // before any element of it has been observed. Scanning tracked element
        // entries only ever finds the elements some earlier access happened to
        // form, which would make the array's shape depend on the order of the
        // proof rather than on its C++ type (ARCHITECTURE.md ARCH-ELEM-004).
        Type indexed;
        if (element == nullptr) {
            indexed = declared_place_type(declaration, prefix, state);
            if (indexed.representation.kind == source::RepresentationKind::Array && !indexed.projections.empty()) {
                extent = static_cast<std::uint32_t>(indexed.projections.size());
                element = &indexed.projections.front();
            }
        }
        if (element == nullptr) {
            rejection = "this subscript's array is not tracked storage of this body, so the extent its index must "
                        "lie within is unknown";
            return std::nullopt;
        }
        Local entry;
        entry.declaration = declaration;
        entry.version = next_version++;
        entry.type = *element;
        entry.path = access.path;
        entry.spelling = access.receiver ? "this->?[?]" : take(clang_getCursorSpelling(declaration)) + "[?]";
        entry.symbolic = true;
        entry.index_value.push_back(std::move(selected));
        // An element of caller storage -- the implicit object's, above all -- is
        // caller storage too: another reference may reach it, and nothing
        // closes the accounting of its writes here (SPEC.md CLASS-010).
        entry.external = std::ranges::any_of(state, [&](const Local& candidate) {
            return candidate.external && clang_equalCursors(candidate.declaration, declaration) != 0;
        });
        // The obligation compares the index against the extent, so the extent
        // is stated at the index's own type: this array's extent is a count
        // Clang resolved, and it enters the comparison as the literal it is
        // rather than as a separately typed quantity (SPEC.md STORAGE-005).
        Expr count;
        count.type = entry.index_value.front().type;
        count.location = entry.index_value.front().location;
        count.node = IntLiteral{static_cast<std::int64_t>(extent)};
        entry.extent.push_back(std::move(count));
        state.push_back(std::move(entry));
        return state.size() - 1;
    }

    static std::optional<std::size_t> find_symbolic(const Locals& locals, CXCursor declaration,
                                                    const std::vector<PlaceStep>& path, const Expr& index_value) {
        for (std::size_t index = locals.size(); index > 0; --index) {
            const Local& candidate = locals[index - 1];
            if (candidate.symbolic && clang_equalCursors(candidate.declaration, declaration) != 0 &&
                candidate.path == path && !candidate.index_value.empty() &&
                same_term(candidate.index_value.front(), index_value) && generation_current(locals, candidate)) {
                return index - 1;
            }
        }
        return std::nullopt;
    }

    // The standard-library models this body's lowering used (RFC 0020 §10).
    std::set<source::RepresentationKind> library_models;

    // Form, or find, the element place a subscript of a modeled sequence names
    // (RFC 0020 §3, SPEC.md STDMODEL-012).
    //
    // The place belongs to the storage the object owns or views, at that
    // storage's current generation, and owes `index < length` where it is
    // formed, against the length of the object subscripted: a vector's own, or
    // a span's. A span parameter's elements are caller storage reached only
    // under the capability the contract states, which is checked on every
    // access, not only the first, since an unsafe block revokes it.
    std::optional<std::size_t> resolve_sequence_element(CXCursor cursor, Locals& state, Capability::Kind required) {
        const std::optional<SequenceCall> call = sequence_call(strip_parens(cursor));
        if (!call || call->constructor || call->name != "operator[]" || call->arguments.size() != 1 ||
            !source::is_sequence(call->family)) {
            return reject("this subscript does not name a modeled container element");
        }
        auto region = element_region(call->object, state, signature);
        if (!region) {
            return reject(region.error());
        }
        library_models.insert(call->family);
        if (region->parameter.has_value() && !granted(*region->parameter, required)) {
            const std::string spelled = take(clang_getCursorSpelling(region->declaration));
            const std::string kind = required == Capability::Kind::Writable ? "writable(" : "readable(";
            return reject(std::string(required == Capability::Kind::Writable ? "writing an element of '"
                                                                             : "reading an element of '") +
                          spelled + "' requires '" + kind + spelled + ")', " +
                          (revoked_by.has_value()
                               ? "which no longer holds after the unsafe block at " + revoked_by->file + ":" +
                                     std::to_string(revoked_by->line) +
                                     ": what that block did to the storage was not checked"
                               : "which was not established: a span does not make the storage it views valid (SPEC.md "
                                 "STDMODEL-016)"));
        }
        const auto access = resolve_access(strip_parens(cursor));
        Expr length = region_length(*region, state, cursor);
        std::optional<Expr> index = access ? element_index(*access, length.type, signature, state) : std::nullopt;
        if (!index) {
            return reject("this subscript's index could not be resolved");
        }
        if (const auto existing = find_element(state, *region, access->path, *index)) {
            return existing;
        }
        Local entry;
        entry.declaration = region->declaration;
        entry.version = next_version++;
        entry.type = region->element;
        entry.path = access->path;
        entry.spelling = take(clang_getCursorSpelling(clang_getCursorReferenced(strip_parens(call->object)))) + "[...]";
        entry.external = region->external;
        entry.symbolic = true;
        entry.index_value.push_back(std::move(*index));
        entry.extent.push_back(std::move(length));
        if (region->root.has_value()) {
            entry.formed_at = Local::Generation{*region->root, state[*region->root].version};
        }
        state.push_back(std::move(entry));
        return state.size() - 1;
    }

    // Form the places an expression reads -- dereferences and element places
    // -- and record each new one, so the statement binds it before anything
    // reads it (see `bind_formed_derefs`).
    bool materialize(CXCursor cursor, Locals& state) {
        const std::size_t before = state.size();
        if (!materialize_derefs(cursor, state)) {
            return false;
        }
        for (std::size_t index = before; index < state.size(); ++index) {
            if (state[index].is_deref() || state[index].symbolic) {
                formed_derefs.push_back(state[index]);
            }
        }
        return true;
    }

    // Mark every entry a container operation in `root` may write, for a loop
    // that carries them (RFC 0020 §4, SPEC.md 24.2). A mutator writes the
    // container's root and whatever may alias it; an element write reaches
    // what the element place may alias; a container passed by mutable
    // reference reaches both; a move writes the container moved from. Which
    // entries those are is the alias analysis's answer, the same one the
    // lowering gives, so a loop carries exactly what an iteration may change.
    void mark_sequence_writes(CXCursor root, const Locals& locals, std::vector<bool>& written, unsigned depth = 0) {
        if (depth > kMaxExpressionDepth) {
            return;
        }
        const auto reach = [&](const Local& target) {
            for (std::size_t index = 0; index < locals.size(); ++index) {
                if (!locals[index].referent.has_value() && may_alias(target, locals[index])) {
                    written[index] = true;
                }
            }
        };
        const auto container = [&](CXCursor object) {
            if (const auto found = owning_root(object, locals)) {
                written[*found] = true;
                reach(locals[*found]);
            }
        };
        const CXCursorKind kind = clang_getCursorKind(root);
        if (const std::optional<SequenceCall> call = sequence_call(root)) {
            if (!call->constructor && is_mutator(*call)) {
                container(call->object);
                if (call->name == "operator=" && call->arguments.size() == 1) {
                    if (const auto moved = moved_operand_of(call->arguments.front())) {
                        container(*moved);
                    }
                }
            }
            if (call->constructor && call->arguments.size() == 1) {
                if (const auto moved = moved_operand_of(call->arguments.front())) {
                    container(*moved);
                }
            }
        } else if (kind == CXCursor_CallExpr) {
            const std::vector<CXCursor> formals = parameters_of(clang_getCursorReferenced(root));
            for (std::size_t index = 0; index < formals.size(); ++index) {
                if (source::may_write(passing_of(clang_getCursorType(formals[index])))) {
                    container(clang_Cursor_getArgument(root, static_cast<unsigned>(index)));
                }
            }
        }
        const bool writes =
            (kind == CXCursor_BinaryOperator && clang_getCursorBinaryOperatorKind(root) == CXBinaryOperator_Assign) ||
            kind == CXCursor_CompoundAssignOperator ||
            (kind == CXCursor_UnaryOperator && (clang_getCursorUnaryOperatorKind(root) == CXUnaryOperator_PreInc ||
                                                clang_getCursorUnaryOperatorKind(root) == CXUnaryOperator_PostInc ||
                                                clang_getCursorUnaryOperatorKind(root) == CXUnaryOperator_PreDec ||
                                                clang_getCursorUnaryOperatorKind(root) == CXUnaryOperator_PostDec));
        if (writes) {
            const std::vector<CXCursor> operands = children_of(root);
            const std::optional<SequenceCall> subscript = !operands.empty() && is_sequence_subscript(operands.front())
                                                              ? sequence_call(strip_parens(operands.front()))
                                                              : std::nullopt;
            if (subscript.has_value()) {
                if (auto region = element_region(subscript->object, locals, signature)) {
                    Local element;
                    element.declaration = region->declaration;
                    element.path = {PlaceStep{PlaceStep::Kind::SymbolicElement, 0, 0}};
                    element.external = region->external;
                    element.symbolic = true;
                    element.type = region->element;
                    reach(element);
                }
            }
        }
        for (const CXCursor child : children_of(root)) {
            mark_sequence_writes(child, locals, written, depth + 1);
        }
    }

    // Whether `cursor` is a subscript of a vector, a string or a span.
    [[nodiscard]] static bool is_sequence_subscript(CXCursor cursor) {
        const std::optional<SequenceCall> call = sequence_call(strip_parens(cursor));
        return call && !call->constructor && call->name == "operator[]" && source::is_sequence(call->family);
    }

    // Form the place of every dereference an expression reads, so the read
    // resolves to storage rather than to an opaque value.
    //
    // A read requires `readable`. The write target is handled separately, by
    // `written_local`, because writing requires `writable` and neither
    // capability entails the other (RFC 0014 §3).
    bool materialize_derefs(CXCursor cursor, Locals& state, unsigned depth = 0) {
        if (depth > kMaxExpressionDepth) {
            return true;
        }
        const auto kind = clang_getCursorKind(cursor);
        // A subscript of a vector, a string or a span forms its element place
        // at the current generation, owing its bound (RFC 0020 §3).
        if (is_sequence_subscript(cursor)) {
            if (!resolve_sequence_element(cursor, state, Capability::Kind::Readable)) {
                return false;
            }
            const std::optional<SequenceCall> subscript = sequence_call(strip_parens(cursor));
            return !subscript || subscript->arguments.empty() ||
                   materialize_derefs(subscript->arguments.front(), state, depth + 1);
        }
        // A subscript of a tracked `std::array` is its element place, exactly as
        // a built-in array's is; an untracked one is observed as a value.
        if (kind == CXCursor_CallExpr) {
            if (const std::optional<SequenceCall> call = sequence_call(cursor);
                call && call->family == source::RepresentationKind::StdArray && !call->constructor) {
                library_models.insert(source::RepresentationKind::StdArray);
                if (call->name == "operator[]" && call->arguments.size() == 1) {
                    const auto access = resolve_access(cursor);
                    const bool tracked = access && std::ranges::any_of(state, [&](const Local& entry) {
                                             return clang_equalCursors(entry.declaration,
                                                                       clang_getCursorReferenced(access->object)) != 0;
                                         });
                    if (tracked && !access->symbolic_indices.empty() && !resolve_symbolic_element(state, *access)) {
                        return false;
                    }
                    return materialize_derefs(call->arguments.front(), state, depth + 1);
                }
            }
        }
        // A symbolic subscript of a tracked array forms its own place.
        if (kind == CXCursor_ArraySubscriptExpr) {
            if (const auto access = resolve_access(cursor);
                access && !access->dereferenced && !access->symbolic_indices.empty()) {
                if (!resolve_symbolic_element(state, *access)) {
                    return false;
                }
                return materialize_derefs(children_of(cursor)[1], state, depth + 1);
            }
        }
        // `this` is a pointer the body never dereferences as one: `this->x`,
        // `(*this).x` and `this->f()` reach the implicit object, which is the
        // receiver's own tracked storage and owes no capability (SPEC.md
        // CLASS-008).
        const std::vector<CXCursor> operands = children_of(cursor);
        const bool through_this = !operands.empty() && this_record(operands.front()).has_value();
        const bool dereferences =
            !through_this &&
            ((kind == CXCursor_UnaryOperator && clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_Deref) ||
             ((kind == CXCursor_MemberRefExpr || kind == CXCursor_ArraySubscriptExpr) && !operands.empty() &&
              clang_getCanonicalType(clang_getCursorType(strip_parens(operands.front()))).kind == CXType_Pointer));
        if (dereferences) {
            if (const auto access = resolve_access(cursor); access && access->dereferenced) {
                if (!resolve_storage(cursor, state, Capability::Kind::Readable)) {
                    if (rejection.empty()) {
                        rejection = "dereferencing a pointer requires a memory capability this implementation "
                                    "could not resolve";
                    }
                    return false;
                }
                return true;
            }
            rejection = "dereferencing this expression requires a pointer whose storage this implementation "
                        "can identify";
            return false;
        }
        for (const auto child : children_of(cursor)) {
            if (!materialize_derefs(child, state, depth + 1)) {
                return false;
            }
        }
        return true;
    }

    // Havoc uses the same version namespace as exact writes. No premise is
    // inherited for the new value; old facts still name only old versions.
    Expr unknown(const Locals& state, std::size_t entry, Expr body, CXCursor at, bool confined = false) {
        Expr result;
        result.type = body.type;
        result.location = presumed_location(clang_getCursorLocation(at));
        result.node = UnknownVersion{
            state[entry].version, place_of(state, entry), state[entry].type, {std::move(body)}, confined};
        return result;
    }

    // Whether a write to `target` may reach `other`, so facts about `other`
    // cannot survive it (SPEC.md 12.10, RFC 0014 §4).
    //
    // Disjointness is proved, never assumed, and only from what Clang
    // resolves. Two places rooted in distinct locals are disjoint because no
    // two locals share storage. Within one object, paths that differ at some
    // step are disjoint because they select different members. A write to an
    // object reaches the members inside it, and a write to a member reaches
    // the object it belongs to, because they are the same storage seen at
    // different granularity.
    //
    // Everything else may alias. Two by-reference parameters may designate one
    // object, so a write through either invalidates the other. No type-based
    // argument is used: strict aliasing is valid C++ inference, but it
    // presupposes the undefined-behavior freedom a proof has not established,
    // so using it here would make the proof circular (AGENTS.md storage
    // invariants).
    // A dereference designates storage this body cannot name, so it is the
    // conservative case: two dereferences may always alias, and a dereference
    // may alias any storage whose address could have reached a pointer. Only
    // the address-taken locals are at risk, because a local whose address is
    // never taken cannot be the pointee of any pointer -- and that is a fact
    // Clang resolves, not a type-based argument (RFC 0014 §4).
    [[nodiscard]] bool may_alias(const Local& target, const Local& other) const {
        // A container's modeled value is its length, and its object's storage
        // is disjoint from every live scalar object: a scalar holds no other
        // object, and a container's elements live in storage the container
        // allocated rather than in the container object (C++ [intro.object]).
        // So a write to a scalar place -- an element, a local, a reference's
        // referent -- never reaches a container's root (RFC 0020 §3). A write
        // through a pointer is not such a write: the pointer's declared pointee
        // type is no evidence about the object it designates.
        if (other.sequence.has_value() && !target.sequence.has_value() && !target.is_deref()) {
            return false;
        }
        if (target.is_deref() || other.is_deref()) {
            if (target.is_deref() && other.is_deref()) {
                // Same pointer and same pointer version: one place, so the
                // path decides. Otherwise two unrelated pointees, which may
                // overlap for all this implementation can prove.
                if (target.pointer == other.pointer && target.pointer_version == other.pointer_version) {
                    return target.covered_by(other) || other.covered_by(target);
                }
                return true;
            }
            const Local& storage = target.is_deref() ? other : target;
            return storage.external || escaped.contains(clang_hashCursor(storage.declaration));
        }
        if (clang_equalCursors(target.declaration, other.declaration) != 0) {
            // A symbolic index selects an element this implementation cannot
            // decide, so two element places of one array may be the same
            // element unless their indices are proved unequal. That proof does
            // not exist here, so they are assumed to overlap: a false rejection
            // is preferable to a stale fact (RFC 0014 §4, AGENTS.md storage
            // invariants).
            if (target.has_symbolic_step() || other.has_symbolic_step()) {
                return true;
            }
            return target.covered_by(other) || other.covered_by(target);
        }
        // Distinct locals never share storage. A by-reference parameter
        // designates caller storage, which any other such parameter may
        // designate too.
        return target.external && other.external;
    }

    // Gives a container's root a new storage generation, recording what
    // established it for the diagnostic of a view used after it, and returns
    // the call effect that writes the root in the call's first position
    // (STDMODEL-015).
    CallEffect new_generation(Local& root, std::string reason) {
        root.version = next_version++;
        if (root.sequence.has_value()) {
            root.sequence->invalidated = std::move(reason);
        }
        return CallEffect{0, root.version, root.type};
    }

    std::vector<std::size_t> invalidate_aliases(std::size_t storage, Locals& state) {
        std::vector<std::size_t> changed;
        const Local target = state[storage];
        for (std::size_t index = 0; index < state.size(); ++index) {
            if (index == storage || state[index].referent.has_value()) {
                continue;
            }
            if (Local& reached = state[index]; may_alias(target, reached)) {
                reached.version = next_version++;
                // A container reached this way has a new storage generation
                // too, and a view of it formed before is stale (STDMODEL-015).
                if (reached.sequence.has_value()) {
                    reached.sequence->invalidated =
                        "a write to '" + target.spelling + "', which may designate the same container";
                }
                changed.push_back(index);
            }
        }
        return changed;
    }

    // The storage an argument hands a callee without passing the container:
    // the root of the tracked container a span or a data pointer is over, or
    // the span parameter it passes on (RFC 0020 §7).
    struct HandedStorage {
        std::optional<std::size_t> root;
        std::optional<CXCursor> span_parameter;
        source::RepresentationKind family = source::RepresentationKind::None;
    };

    [[nodiscard]] std::optional<HandedStorage> handed_storage(CXCursor argument, const Locals& state) const {
        CXCursor stripped = strip_parens(argument);
        if (clang_getCursorKind(stripped) == CXCursor_CXXFunctionalCastExpr) {
            for (const CXCursor child : children_of(stripped)) {
                if (clang_isExpression(clang_getCursorKind(child)) != 0) {
                    stripped = strip_parens(child);
                    break;
                }
            }
        }
        if (const std::optional<SequenceCall> call = sequence_call(stripped)) {
            if (call->constructor && call->family == source::RepresentationKind::Span && call->arguments.size() == 1) {
                if (const std::optional<std::size_t> root = owning_root(call->arguments.front(), state)) {
                    return HandedStorage{root, std::nullopt, source::RepresentationKind::Span};
                }
                return std::nullopt;
            }
            if (!call->constructor && call->name == "data" && source::is_sequence(call->family)) {
                stripped = strip_parens(call->object);
            } else {
                return std::nullopt;
            }
        }
        if (clang_getCursorKind(stripped) != CXCursor_DeclRefExpr) {
            return std::nullopt;
        }
        const CXCursor declaration = clang_getCursorReferenced(stripped);
        if (const auto binding = find_binding(state, declaration)) {
            const std::size_t storage = state[*binding].referent.value_or(*binding);
            const std::optional<Local::Sequence>& held = state[storage].sequence;
            if (!held.has_value()) {
                return std::nullopt;
            }
            return HandedStorage{held->views.value_or(storage), std::nullopt, held->kind};
        }
        const Type type = convert_type(clang_getCursorType(declaration));
        if (type.representation.kind == source::RepresentationKind::Span &&
            std::ranges::any_of(parameters,
                                [&](CXCursor candidate) { return clang_equalCursors(candidate, declaration); })) {
            return HandedStorage{std::nullopt, declaration, source::RepresentationKind::Span};
        }
        return std::nullopt;
    }

    // What a verified call owes for the container storage it hands its callee
    // as a span or a data pointer (RFC 0020 §7, SPEC.md STDMODEL-016,
    // STDMODEL-017).
    //
    // The storage a capability designates must not be element storage of a
    // container the callee may reallocate: a callee may keep reading a span
    // while it appends to a vector it holds by reference only because the two
    // are proved apart here. A callee that may write through what it is handed
    // leaves the elements unknown afterwards, and could store an unrefined value
    // in a refined container, so that is refused.
    std::optional<std::string> view_arguments(CXCursor call, const std::vector<CXCursor>& formals, Locals& state,
                                              std::vector<std::size_t>& invalidated) {
        // The containers the callee receives by mutable reference, and so may
        // reallocate.
        std::vector<std::size_t> reallocatable;
        for (std::size_t index = 0; index < formals.size(); ++index) {
            if (!source::may_write(passing_of(clang_getCursorType(formals[index])))) {
                continue;
            }
            if (const auto root = owning_root(clang_Cursor_getArgument(call, static_cast<unsigned>(index)), state)) {
                reallocatable.push_back(*root);
            }
        }
        for (std::size_t index = 0; index < formals.size(); ++index) {
            const CXCursor argument = clang_Cursor_getArgument(call, static_cast<unsigned>(index));
            const CXType written = clang_getCanonicalType(clang_getCursorType(formals[index]));
            const Type formal = convert_type(written);
            const bool span = formal.representation.kind == source::RepresentationKind::Span;
            const bool pointer = written.kind == CXType_Pointer;
            if (!span && !pointer) {
                continue;
            }
            const std::optional<HandedStorage> handed = handed_storage(argument, state);
            if (!handed) {
                continue;
            }
            library_models.insert(handed->family);
            // Whether the callee may write the elements through it: a span of
            // non-const elements, or a pointer to non-const.
            const CXType element =
                span ? clang_Type_getTemplateArgumentAsType(written, 0) : clang_getPointeeType(written);
            const bool writes = clang_isConstQualifiedType(element) == 0;
            if (handed->root.has_value()) {
                const std::size_t root = *handed->root;
                for (const std::size_t other : reallocatable) {
                    if (other == root || may_alias(state[other], state[root])) {
                        return "'" + state[root].spelling + "' is handed to '" +
                               qualified_name_of(clang_getCursorReferenced(call)) +
                               "' as a view or data pointer and, in the same call, by a reference through which the "
                               "callee may reallocate it; the storage a capability designates must not be storage "
                               "the callee can replace (SPEC.md STDMODEL-016)";
                    }
                }
                if (!writes) {
                    continue;
                }
                if (!state[root].sequence->element.refinements.empty()) {
                    return "the elements of '" + state[root].spelling +
                           "' are handed to a callee that may write them, and nothing obliges it to write values "
                           "satisfying '" +
                           state[root].sequence->element.refinements.front().name + "'";
                }
                // Every element place of the container is unknown after the
                // call; the container's length is not, since no element write
                // changes it.
                for (std::size_t other = 0; other < state.size(); ++other) {
                    if (state[other].referent.has_value() || !state[other].formed_at.has_value() ||
                        state[other].formed_at->root != root) {
                        continue;
                    }
                    state[other].version = next_version++;
                    invalidated.push_back(other);
                    for (const std::size_t aliased : invalidate_aliases(other, state)) {
                        invalidated.push_back(aliased);
                    }
                }
            } else if (handed->span_parameter.has_value() && writes) {
                for (std::size_t other = 0; other < state.size(); ++other) {
                    if (state[other].referent.has_value() ||
                        clang_equalCursors(state[other].declaration, *handed->span_parameter) == 0 ||
                        state[other].path.empty()) {
                        continue;
                    }
                    state[other].version = next_version++;
                    invalidated.push_back(other);
                    for (const std::size_t aliased : invalidate_aliases(other, state)) {
                        invalidated.push_back(aliased);
                    }
                }
            }
        }
        return std::nullopt;
    }

    // Evaluate a full expression once, then advance the storage touched by its
    // call. The continuation sees only these post-call versions.
    std::optional<Expr> evaluate(CXCursor cursor, Locals& state, std::vector<std::size_t>& invalidated) {
        while (clang_getCursorKind(cursor) == CXCursor_UnexposedExpr ||
               clang_getCursorKind(cursor) == CXCursor_ParenExpr) {
            const auto children = children_of(cursor);
            if (children.size() != 1 || !same_modeled_value(convert_type(clang_getCursorType(cursor)),
                                                            convert_type(clang_getCursorType(children.front()))))
                break;
            cursor = children.front();
        }
        const std::size_t before = state.size();
        if (!materialize_derefs(cursor, state)) {
            return std::nullopt;
        }
        for (std::size_t index = before; index < state.size(); ++index) {
            if (state[index].is_deref() || state[index].symbolic) {
                formed_derefs.push_back(state[index]);
            }
        }
        Expr value = build_expression(cursor, signature, state, 0, true);
        auto* call = std::get_if<Call>(&value.node);
        if (!call)
            return value;
        const auto callee = clang_getCursorReferenced(cursor);
        const auto params = parameters_of(callee);
        if (!call->library.has_value()) {
            if (std::optional<std::string> refused = view_arguments(cursor, params, state, invalidated)) {
                return reject(std::move(*refused));
            }
        }
        // A callee that takes a pointer to non-const may write through it, and
        // the caller's facts about the pointee do not survive that. This is
        // separate from the reference case below: a pointer is passed by value,
        // so the parameter keeps its own version and it is the storage it
        // designates that goes stale (SPEC.md 12.10 VERIFIED-040).
        for (std::size_t index = 0; index < params.size(); ++index) {
            const CXType declared = clang_getCursorType(params[index]);
            if (source::aliases_storage(passing_of(declared)) || !may_write_through(declared))
                continue;
            const CXCursor argument = clang_Cursor_getArgument(cursor, static_cast<unsigned>(index));
            // The pointer is only read here, so it is looked up rather than
            // resolved as a write target: passing a pointer owes no capability
            // of its own, and demanding one would refuse the call outright.
            const auto access = resolve_access(strip_parens(argument));
            if (!access || access->dereferenced || !access->symbolic_indices.empty())
                continue;
            // A deref place records the entry holding its pointer, and a
            // pointer parameter is identified by its own index rather than by a
            // tracked local, exactly as `resolve_storage` roots one.
            const CXCursor declaration = access->declaration;
            auto pointer = find_binding(state, declaration, access->path);
            if (!pointer) {
                const auto at = std::ranges::find_if(
                    parameters, [&](CXCursor candidate) { return clang_equalCursors(candidate, declaration) != 0; });
                if (at == parameters.end())
                    continue;
                pointer = static_cast<std::size_t>(at - parameters.begin());
            }
            // Every place reached through this pointer, and everything that may
            // alias one, is unknown from here on. Which of them the callee
            // actually wrote is not stated by its contract, so none is kept.
            for (std::size_t other = 0; other < state.size(); ++other) {
                if (state[other].referent.has_value() || !state[other].is_deref())
                    continue;
                if (state[other].pointer != pointer)
                    continue;
                state[other].version = next_version++;
                invalidated.push_back(other);
                for (const auto aliased : invalidate_aliases(other, state)) {
                    invalidated.push_back(aliased);
                }
            }
        }
        // A member function called on an object takes the object's leaves as
        // the arguments of its implicit object (SPEC.md CLASS-011). The build
        // above resolved both already, or the value would not be a call.
        std::optional<Receiver> callee_receiver;
        std::optional<CallObject> object;
        if (clang_getCursorKind(callee) == CXCursor_CXXMethod && clang_CXXMethod_isStatic(callee) == 0) {
            auto receiver = receiver_of(callee, nullptr);
            auto resolved = call_object(cursor, callee);
            if (!receiver || !resolved) {
                return reject("the object of a member call was not resolved");
            }
            callee_receiver = std::move(*receiver);
            object = std::move(*resolved);
        }
        const std::uint32_t offset =
            callee_receiver.has_value() ? static_cast<std::uint32_t>(callee_receiver->leaves.size()) : 0;
        const bool writes = (callee_receiver.has_value() && callee_receiver->writes()) ||
                            std::ranges::any_of(params, [](CXCursor parameter) {
                                return source::may_write(passing_of(clang_getCursorType(parameter)));
                            });
        if (!writes)
            return value;
        std::vector<std::size_t> targets;
        // Shared actual arguments must share one post-state value.
        const auto target_version = [&](std::size_t storage) {
            if (std::ranges::find(targets, storage) == targets.end()) {
                targets.push_back(storage);
                state[storage].version = next_version++;
                // A container the callee holds by mutable reference may be
                // reallocated there: it has a new storage generation
                // (STDMODEL-015).
                if (std::optional<Local::Sequence>& held = state[storage].sequence; held.has_value()) {
                    held->invalidated = "passing it by mutable reference to '" + qualified_name_of(callee) + "' at " +
                                        describe_location(cursor);
                    library_models.insert(held->kind);
                }
            }
            return state[storage].version;
        };
        if (callee_receiver.has_value()) {
            for (std::size_t leaf = 0; leaf < callee_receiver->leaves.size(); ++leaf) {
                std::vector<PlaceStep> path = object->path;
                path.insert(path.end(), callee_receiver->leaves[leaf].path.begin(),
                            callee_receiver->leaves[leaf].path.end());
                const auto target = find_local(state, object->declaration, path);
                if (!target) {
                    return reject("the object of a member call has storage this body does not track where '" +
                                  callee_receiver->leaves[leaf].spelling + "' stands");
                }
                const std::uint32_t version = target_version(*target);
                call->effects.push_back(CallEffect{static_cast<std::uint32_t>(leaf), version, state[*target].type});
            }
        }
        for (std::size_t index = 0; index < params.size(); ++index) {
            if (!source::aliases_storage(passing_of(clang_getCursorType(params[index]))))
                continue;
            const auto target = written_local(clang_Cursor_getArgument(cursor, static_cast<unsigned>(index)), state);
            if (!target)
                return std::nullopt;
            const auto storage = state[*target].referent.value_or(*target);
            // A span local handed on by mutable reference could be made to view
            // other storage than the one its generation is followed for.
            if (const std::optional<Local::Sequence>& handed = state[storage].sequence;
                handed.has_value() && handed->views.has_value()) {
                return reject("span '" + state[storage].spelling +
                              "' is passed by mutable reference; a view is modeled only as a value");
            }
            const std::uint32_t version = target_version(storage);
            call->effects.push_back(
                CallEffect{offset + static_cast<std::uint32_t>(index), version, state[storage].type});
        }
        // Every other place of the object may have been written as well: the
        // callee's leaves are the storage its contract speaks of, and whatever
        // else the object holds -- an element formed at a term, a member the
        // callee does not track -- is unknown after the call rather than kept
        // (SPEC.md CLASS-011).
        if (object.has_value()) {
            for (std::size_t other = 0; other < state.size(); ++other) {
                const Local& entry = state[other];
                if (entry.referent || entry.is_deref() || std::ranges::find(targets, other) != targets.end() ||
                    clang_equalCursors(entry.declaration, object->declaration) == 0 ||
                    entry.path.size() < object->path.size() ||
                    !std::equal(object->path.begin(), object->path.end(), entry.path.begin())) {
                    continue;
                }
                state[other].version = next_version++;
                invalidated.push_back(other);
            }
        }
        for (std::size_t other = 0; other < state.size(); ++other) {
            if (!state[other].external || state[other].referent || std::ranges::find(targets, other) != targets.end() ||
                std::ranges::find(invalidated, other) != invalidated.end())
                continue;
            // A written external place may be another external place of its
            // modeled type, and it may be a member of any object a parameter
            // designates. Two members of one object are the exception: they
            // are distinct storage, which Clang resolves (SPEC.md CLASS-010).
            if (std::ranges::any_of(targets, [&](std::size_t target) {
                    return state[target].external &&
                           (same_modeled_value(state[target].type, state[other].type) ||
                            state[other].type.kind == TypeKind::Value) &&
                           !distinct_members(state[target], state[other]);
                })) {
                state[other].version = next_version++;
                invalidated.push_back(other);
            }
        }
        // A callee holding a container by mutable reference may write any of
        // its elements, and may reallocate it: whatever may be that container or
        // one of its elements is unknown after the call (RFC 0020 §3, §4).
        for (const std::size_t target : targets) {
            if (!state[target].sequence.has_value()) {
                continue;
            }
            for (std::size_t other = 0; other < state.size(); ++other) {
                if (other == target || state[other].referent.has_value() ||
                    std::ranges::find(targets, other) != targets.end() ||
                    std::ranges::find(invalidated, other) != invalidated.end() ||
                    !may_alias(state[target], state[other])) {
                    continue;
                }
                state[other].version = next_version++;
                if (state[other].sequence.has_value()) {
                    state[other].sequence->invalidated = state[target].sequence->invalidated;
                }
                invalidated.push_back(other);
            }
        }
        return value;
    }

    // A mutator of a modeled sequence written as a statement (RFC 0020 §6): a
    // call to a trusted library summary whose effect is a new version of the
    // container's root, which is a new storage generation (§4). From here on,
    // no element place formed before is matched and every view or element
    // reference formed before is stale (STDMODEL-015). A value it puts into an
    // element owes the element type's refinement before the call.
    std::optional<Expr> lower_sequence_statement(const SequenceCall& call, CXCursor statement, const Continuation& next,
                                                 const Locals& locals, unsigned depth) {
        using K = source::RepresentationKind;
        using Op = source::LibraryOperation;
        const std::string qualified = library_name(call.family, call.name);
        if (!source::is_sequence(call.family) || call.constructor) {
            return reject("'" + qualified + "' is not a modeled statement (SPEC.md STDMODEL-019)");
        }
        Locals state = locals;
        const std::optional<std::size_t> root = owning_root(call.object, state);
        if (!root) {
            return reject("'" + qualified +
                          "' is modeled only on a vector or string this body names directly (SPEC.md STDMODEL-013)");
        }
        library_models.insert(call.family);
        const std::string name = state[*root].spelling;
        Type element_type;
        {
            // Read before anything is added to `state`, which may move entries.
            const std::optional<Local::Sequence>& rooted = state[*root].sequence;
            if (!rooted.has_value()) {
                return reject("'" + qualified + "' is modeled only on a vector or string this body tracks");
            }
            element_type = rooted->element;
        }
        const std::vector<CXCursor> formals = parameters_of(call.method);
        const auto formal = [&](std::size_t position) {
            return position < formals.size() ? clang_getCanonicalType(clang_getCursorType(formals[position]))
                                             : CXType{CXType_Invalid, {nullptr, nullptr}};
        };
        // Whether a formal parameter is a reference to the container's own
        // class: the overload taking another container of the same type.
        const CXCursor own_class =
            clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(strip_parens(call.object))));
        const auto of_own_class = [&](CXType reference, CXTypeKind kind) {
            return reference.kind == kind &&
                   clang_equalCursors(clang_getTypeDeclaration(clang_getCanonicalType(clang_getPointeeType(reference))),
                                      own_class) != 0;
        };
        std::optional<Op> operation;
        std::optional<CXCursor> pushed_argument;
        std::optional<CXCursor> count_argument;
        std::optional<std::size_t> source;
        // `s += c` appends one character, as `push_back` does.
        const bool appends_character = call.family == K::String && call.name == "operator+=" &&
                                       call.arguments.size() == 1 &&
                                       (formal(0).kind == CXType_Char_S || formal(0).kind == CXType_Char_U);
        if ((call.name == "push_back" && call.arguments.size() == 1) || appends_character) {
            operation = Op::PushBack;
            pushed_argument = call.arguments.front();
        } else if (call.family == K::String && (call.name == "operator+=" || call.name == "append") &&
                   call.arguments.size() == 1 && of_own_class(formal(0), CXType_LValueReference)) {
            operation = Op::Append;
            source = owning_root(call.arguments.front(), state);
        } else if (call.name == "pop_back" && call.arguments.empty()) {
            operation = Op::PopBack;
        } else if (call.name == "clear" && call.arguments.empty()) {
            operation = Op::Clear;
        } else if (call.name == "reserve" && call.arguments.size() == 1) {
            operation = Op::Reserve;
            count_argument = call.arguments.front();
        } else if (call.name == "operator=" && call.arguments.size() == 1 &&
                   (of_own_class(formal(0), CXType_LValueReference) ||
                    of_own_class(formal(0), CXType_RValueReference))) {
            const bool moving = formal(0).kind == CXType_RValueReference;
            operation = moving ? Op::MoveAssign : Op::Assign;
            const std::optional<CXCursor> operand =
                moving ? moved_operand(call.arguments.front()) : call.arguments.front();
            source = operand ? owning_root(*operand, state) : std::optional<std::size_t>{};
        }
        if (!operation) {
            return reject("'" + qualified + "' is not a modeled operation of " +
                          std::string(source::describe_model(call.family)) + " (SPEC.md STDMODEL-019)");
        }
        if ((*operation == Op::Append || *operation == Op::Assign || *operation == Op::MoveAssign) && !source) {
            return reject("'" + qualified + "' is modeled only with a container this body tracks as its argument");
        }
        if (source.has_value() && (*operation == Op::Assign || *operation == Op::MoveAssign)) {
            if (*source == *root) {
                return reject("assigning '" + name + "' to itself is not modeled");
            }
            if (auto gap = refinement_gap(state[*root], state[*source])) {
                return reject(std::move(*gap));
            }
            if (*operation == Op::MoveAssign && state[*source].external) {
                return reject("'" + name + "' is assigned by moving from '" + state[*source].spelling +
                              "', which is caller storage; only a container this body owns is moved from");
            }
        }

        std::optional<Expr> pushed;
        if (pushed_argument) {
            if (!materialize(*pushed_argument, state)) {
                return std::nullopt;
            }
            pushed = build_expression(*pushed_argument, signature, state, 0);
            if (!std::holds_alternative<Unsupported>(pushed->node) && !same_modeled_value(element_type, pushed->type)) {
                return reject("pushing '" + pushed->type.spelling + "' into '" + name + "' of element type '" +
                              element_type.spelling + "' is a conversion that is not modeled");
            }
        }
        std::vector<Expr> arguments{read_root(state, *root, statement), read_root(state, *root, statement)};
        if (count_argument) {
            if (!materialize(*count_argument, state)) {
                return std::nullopt;
            }
            Expr count = build_expression(*count_argument, signature, state, 0);
            if (!std::holds_alternative<Unsupported>(count.node) &&
                !same_modeled_value(state[*root].type.projections.front(), count.type)) {
                return reject("reserving '" + count.type.spelling + "' elements of '" + name +
                              "' is a conversion to its size type that is not modeled");
            }
            arguments.push_back(std::move(count));
        }
        if (source) {
            arguments.push_back(read_root(state, *source, statement));
            if (*operation == Op::MoveAssign) {
                arguments.push_back(read_root(state, *source, statement));
            }
        }

        // Versions in evaluation order: the pushed value, then the effects.
        const std::optional<std::uint32_t> pushed_version =
            pushed ? std::optional<std::uint32_t>{next_version++} : std::nullopt;
        const std::string where = "'" + qualified + "' at " + describe_location(statement);
        std::vector<CallEffect> effects;
        effects.push_back(new_generation(state[*root], where));
        std::vector<std::size_t> invalidated = invalidate_aliases(*root, state);
        // A move assignment has its source: one without was refused above.
        if (*operation == Op::MoveAssign && source.has_value()) {
            CallEffect moved = new_generation(state[*source], "being moved from by " + where);
            moved.argument = 2;
            effects.push_back(std::move(moved));
            for (const std::size_t changed : invalidate_aliases(*source, state)) {
                if (std::ranges::find(invalidated, changed) == invalidated.end()) {
                    invalidated.push_back(changed);
                }
            }
        }
        Type nothing;
        nothing.kind = TypeKind::Void;
        nothing.spelling = "void";
        Expr value = library_call({call.family, *operation}, state[*root].type, qualified, std::move(arguments),
                                  nothing, statement);
        std::get<Call>(value.node).effects = std::move(effects);
        const std::uint32_t discarded = next_version++;

        std::optional<Expr> body = lower_statements(next, state, depth + 1);
        if (!body) {
            return std::nullopt;
        }
        for (const std::size_t changed : std::views::reverse(invalidated)) {
            *body = unknown(state, changed, std::move(*body), statement);
        }
        body = bind(discarded, anonymous_place("library call"), std::move(value), std::move(*body), statement);
        if (pushed) {
            body = bind(*pushed_version, anonymous_place("element pushed into " + name), std::move(*pushed),
                        std::move(*body), statement, element_type);
        }
        return body;
    }

    std::optional<Expr> lower_call(CXCursor statement, const Continuation& next, const Locals& locals, unsigned depth) {
        if (const std::optional<SequenceCall> call = sequence_call(statement)) {
            return lower_sequence_statement(*call, statement, next, locals, depth);
        }
        Locals state = locals;
        std::vector<std::size_t> invalidated;
        auto value = evaluate(statement, state, invalidated);
        if (!value)
            return std::nullopt;
        const auto version = next_version++;
        auto body = lower_statements(next, state, depth + 1);
        if (!body)
            return std::nullopt;
        for (auto index : invalidated)
            *body = unknown(state, index, std::move(*body), statement);
        return bind(version, anonymous_place("discarded call"), std::move(*value), std::move(*body), statement);
    }

    std::nullopt_t reject(std::string reason) {
        if (rejection.empty()) {
            rejection = std::move(reason);
        }
        return std::nullopt;
    }

    // The one write of tracked storage (SPEC.md 12.10, RFC 0014 §5).
    //
    // Establishing a version is what a write is, whatever syntax performed it:
    // a declaration, an assignment, a compound update, a member
    // initialization, a call's effect on an argument. `declared` is the type
    // the place was written with, and it is what the refinement crossing is
    // generated from downstream, at one site rather than per form.
    Expr bind(std::uint32_t version, Place place, Expr value, Expr body, CXCursor at, Type declared = {}) {
        Expr expr;
        expr.type = body.type;
        expr.location = presumed_location(clang_getCursorLocation(at));
        expr.node = PlaceVersion{version, std::move(place), {std::move(value), std::move(body)}, std::move(declared)};
        return expr;
    }

    // A place that is not tracked storage: a call result or another value the
    // body binds without naming storage. It has a version so the value is
    // stated once, and a spelling so diagnostics can name it.
    static Place anonymous_place(std::string spelling) {
        Place place;
        place.root.kind = PlaceRoot::Kind::Local;
        place.root.id = std::numeric_limits<std::uint32_t>::max();
        place.spelling = std::move(spelling);
        return place;
    }

    std::optional<Expr> lower_statements(const Continuation& from, const Locals& locals, unsigned depth) {
        // Each statement lowers the rest of the body inside itself, so this
        // bounds the statements on one path as well as their nesting.
        if (depth > kMaxExpressionDepth) {
            return reject("more than " + std::to_string(kMaxExpressionDepth) +
                          " nested or consecutive statements on one path are not modeled");
        }
        if (from.header != nullptr) {
            return lower_loop(*from.header, locals, depth + 1);
        }
        if (from.iteration != nullptr) {
            return end_iteration(*from.iteration, from.after_increment, locals, depth + 1);
        }
        if (from.index == from.statements->size()) {
            if (from.outer == nullptr) {
                if (result_type.kind == TypeKind::Void) {
                    const CXCursor at = clang_getNullCursor();
                    return completed(void_value(at), locals, at);
                }
                return reject("every path must return a value");
            }
            return lower_statements(*from.outer, locals, depth + 1);
        }
        const CXCursor statement = (*from.statements)[from.index];
        if (const std::optional<std::string> marker = contradiction_marker(statement)) {
            return lower_contradiction(*marker, *from.statements, from.index, locals);
        }
        if (const std::optional<std::string> marker = split_marker(*from.statements, from.index)) {
            return lower_split(*marker, from, locals, depth);
        }
        if (ghost_marker_of(statement, invariant_prefix)) {
            return lower_ghost(from, locals, depth);
        }
        const Continuation next{from.outer, from.statements, from.index + 1};
        // An unsafe block says for itself why a way out of it is refused.
        if (next.index != from.statements->size() && !unsafe_marker_of(statement, invariant_prefix).has_value() &&
            terminates(statement, 0)) {
            return reject("unreachable trailing statements are not modeled");
        }
        return lower_statement(statement, next, locals, depth);
    }

    // Lower one statement, then bind every dereference place it formed.
    //
    // The binding wraps the whole statement's value, so each pointee has an
    // entry value before anything reads it. Doing it here rather than in each
    // statement form is what keeps a dereference from needing a lowering rule
    // of its own (RFC 0014 §17 step 6).
    std::optional<Expr> lower_statement(CXCursor statement, const Continuation& next, const Locals& locals,
                                        unsigned depth) {
        std::vector<Local> enclosing;
        enclosing.swap(formed_derefs);
        std::optional<Expr> lowered = lower_statement_form(statement, next, locals, depth);
        if (lowered) {
            lowered = bind_formed_derefs(std::move(*lowered), statement);
        }
        formed_derefs = std::move(enclosing);
        return lowered;
    }

    std::optional<Expr> lower_statement_form(CXCursor statement, const Continuation& next, const Locals& locals,
                                             unsigned depth) {
        const CXCursorKind kind = clang_getCursorKind(statement);
        if (const std::optional<CXCursor> marker = unsafe_marker_of(statement, invariant_prefix)) {
            return lower_unsafe(statement, *marker, next, locals, depth);
        }
        if (kind == CXCursor_CompoundStmt) {
            const std::vector<CXCursor> nested = children_of(statement);
            return lower_statements(Continuation{&next, &nested, 0}, locals, depth + 1);
        }
        if (kind == CXCursor_CallExpr)
            return lower_call(statement, next, locals, depth);
        // A container mutator whose argument is a temporary stands inside the
        // node Clang adds to destroy that temporary at the statement's end.
        if (kind == CXCursor_UnexposedExpr) {
            const CXCursor inner = strip_parens(statement);
            if (clang_getCursorKind(inner) == CXCursor_CallExpr && sequence_call(inner).has_value()) {
                return lower_call(inner, next, locals, depth);
            }
        }
        if (kind == CXCursor_NullStmt)
            return lower_statements(next, locals, depth + 1);
        if (kind == CXCursor_ReturnStmt) {
            const std::vector<CXCursor> returned = children_of(statement);
            if (returned.empty() && result_type.kind == TypeKind::Void)
                return completed(void_value(statement), locals, statement);
            if (returned.size() != 1)
                return reject("a return requires one value");
            Locals state = locals;
            std::vector<std::size_t> invalidated;
            auto value = evaluate(returned.front(), state, invalidated);
            if (!value)
                return std::nullopt;
            const auto* call = std::get_if<Call>(&value->node);
            if (call == nullptr || call->effects.empty())
                return completed(std::move(*value), state, statement);
            const auto version = next_version++;
            Expr read;
            read.type = value->type;
            read.location = value->location;
            read.node = PlaceRef{version, anonymous_place("return value")};
            Expr body = completed(std::move(read), state, statement);
            for (auto changed : invalidated)
                body = unknown(state, changed, std::move(body), statement);
            return bind(version, anonymous_place("return value"), std::move(*value), std::move(body), statement);
        }
        if (kind == CXCursor_DeclStmt) {
            // The projector puts one declaration in a templated body to make
            // C++ instantiate that specialization's contract probes with it.
            // It names a probe and computes nothing, so it is not a statement
            // of the program being verified and is stepped over rather than
            // modeled (SPEC.md TEMPLATE-001).
            if (is_instantiation_marker(statement)) {
                return lower_statements(next, locals, depth + 1);
            }
            return lower_declaration(children_of(statement), 0, next, locals, depth);
        }
        if (kind == CXCursor_BinaryOperator &&
            clang_getCursorBinaryOperatorKind(statement) == CXBinaryOperator_Assign) {
            return lower_assignment(statement, next, locals, depth);
        }
        if (kind == CXCursor_CompoundAssignOperator || kind == CXCursor_UnaryOperator) {
            return lower_update(statement, next, locals, depth);
        }
        const std::vector<CXCursor> parts = children_of(statement);
        if (kind == CXCursor_IfStmt && (parts.size() == 2 || parts.size() == 3) &&
            clang_isExpression(clang_getCursorKind(parts[0])) != 0) {
            return lower_branch(statement, parts, next, locals, depth);
        }
        if (kind == CXCursor_WhileStmt) {
            if (parts.size() != 2 || clang_isExpression(clang_getCursorKind(parts[0])) == 0) {
                return reject("a while loop whose condition declares a variable is not modeled");
            }
            const LoopHeader header{statement, parts[0], parts[1], std::nullopt, &next};
            return lower_loop(header, locals, depth);
        }
        if (kind == CXCursor_ForStmt) {
            return lower_for(statement, next, locals, depth);
        }
        if (kind == CXCursor_BreakStmt) {
            return lower_break(locals, depth);
        }
        if (kind == CXCursor_ContinueStmt) {
            if (frames.empty()) {
                return reject("'continue' outside a modeled loop");
            }
            return end_iteration(*frames.back(), false, locals, depth);
        }
        if (kind == CXCursor_DoStmt) {
            if (parts.size() != 2 || clang_isExpression(clang_getCursorKind(parts[1])) == 0) {
                return reject("the parts of this do loop could not be resolved");
            }
            const LoopHeader header{statement, parts[1], parts[0], std::nullopt, &next, true};
            return lower_loop(header, locals, depth);
        }
        if (kind == CXCursor_CXXForRangeStmt) {
            return reject("range-based for loops are not modeled");
        }
        return reject(unmodeled_statement(statement_name(kind)));
    }

    std::optional<Expr> lower_for(CXCursor statement, const Continuation& next, const Locals& locals, unsigned depth) {
        const std::optional<ForParts> parts = for_parts(statement);
        if (!parts) {
            return reject("the parts of this for loop could not be resolved");
        }
        if (parts->condition && clang_isExpression(clang_getCursorKind(*parts->condition)) == 0) {
            return reject("a for loop whose condition declares a variable is not modeled");
        }
        // A `for` without a condition runs until a `break` or a `return` leaves
        // it (SPEC.md LOOP-001).
        const LoopHeader header{statement, parts->condition.value_or(clang_getNullCursor()), parts->body,
                                parts->increment, &next};
        if (!parts->initialization) {
            return lower_loop(header, locals, depth);
        }
        // The initialization runs once, before the loop, with the loop as what
        // follows it.
        Continuation entered;
        entered.header = &header;
        return lower_statement(*parts->initialization, entered, locals, depth);
    }

    // The places an unsafe block could have written: a pointee, the storage a
    // reference parameter designates, and any local whose address this body
    // takes or that an unsafe block of this body names. The last set is the
    // `escaped` one, which `extract_body` widens by every name an unsafe block
    // uses, because such a block may keep an address and write through it later
    // (TRUST.md TCB-UNSAFE-002). A reference is followed to its storage.
    [[nodiscard]] std::vector<std::size_t> unsafe_reach(const Locals& locals) const {
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
    std::optional<Expr> lower_unsafe(CXCursor block, CXCursor marker, const Continuation& next, const Locals& locals,
                                     unsigned depth) {
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

    // The name of the block the projector emitted for a claim that this path
    // cannot occur, if `statement` is the declaration that opens one
    // (SPEC.md VERIFIED-023).
    [[nodiscard]] std::optional<std::string> contradiction_marker(CXCursor statement) const {
        if (invariant_prefix.empty() || clang_getCursorKind(statement) != CXCursor_DeclStmt) {
            return std::nullopt;
        }
        const std::vector<CXCursor> declared = children_of(statement);
        if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
            return std::nullopt;
        }
        std::string name = take(clang_getCursorSpelling(declared[0]));
        if (!name.starts_with(invariant_prefix + "contradiction_") || name.find("_argument_") != std::string::npos) {
            return std::nullopt;
        }
        return name;
    }

    // `contradiction evidence;` written here: this path ends. What follows it
    // is not lowered, because the claim is that nothing after it is reached; the
    // claim itself is the obligation. The evidence's arguments are the block's
    // remaining declarations, each read at the versions current here.
    std::optional<Expr> lower_contradiction(const std::string& marker, const std::vector<CXCursor>& statements,
                                            std::size_t index, const Locals& locals) {
        PathContradiction claim;
        claim.marker = marker;
        for (std::size_t position = index + 1; position < statements.size(); ++position) {
            const std::vector<CXCursor> declared = children_of(statements[position]);
            const std::string expected = marker + "_argument_" + std::to_string(position - index - 1);
            if (clang_getCursorKind(statements[position]) != CXCursor_DeclStmt || declared.size() != 1 ||
                clang_getCursorKind(declared[0]) != CXCursor_VarDecl ||
                take(clang_getCursorSpelling(declared[0])) != expected) {
                return reject("the arguments of this contradiction were not resolved");
            }
            const CXCursor initializer = clang_Cursor_getVarDeclInitializer(declared[0]);
            if (clang_Cursor_isNull(initializer) != 0) {
                return reject("an argument of this contradiction was not resolved");
            }
            claim.operands.push_back(build_expression(initializer, signature, locals, 0));
        }
        consumed_contradictions.push_back(marker);
        Expr ended;
        ended.type = result_type;
        ended.location = presumed_location(clang_getCursorLocation(statements[index]));
        ended.node = std::move(claim);
        return ended;
    }

    // The one variable a generated declaration statement declares, when it
    // declares exactly one and it has this name.
    [[nodiscard]] static std::optional<CXCursor> declared_as(CXCursor statement, std::string_view name) {
        if (clang_getCursorKind(statement) != CXCursor_DeclStmt) {
            return std::nullopt;
        }
        const std::vector<CXCursor> declared = children_of(statement);
        if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl ||
            take(clang_getCursorSpelling(declared[0])) != name) {
            return std::nullopt;
        }
        return declared[0];
    }

    // The name of the block the projector emitted for a case split on this
    // path, if the statement at `index` opens one: a generated `bool` followed
    // by the split's subject (SPEC.md CASE-017).
    [[nodiscard]] std::optional<std::string> split_marker(const std::vector<CXCursor>& statements,
                                                          std::size_t index) const {
        if (invariant_prefix.empty() || index + 1 >= statements.size() ||
            clang_getCursorKind(statements[index]) != CXCursor_DeclStmt) {
            return std::nullopt;
        }
        const std::vector<CXCursor> declared = children_of(statements[index]);
        if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
            return std::nullopt;
        }
        std::string name = take(clang_getCursorSpelling(declared[0]));
        if (!name.starts_with(invariant_prefix + "split_") || !declared_as(statements[index + 1], name + "_subject")) {
            return std::nullopt;
        }
        return name;
    }

    // A case split written here: the subject is read at the versions current
    // here, and each arm continues this path, through its own nested splits and
    // claims and then through the rest of the body after the split. Nothing
    // about the representation's states is decided here; the arms are carried
    // as written, with each arm's binders standing for the values its case
    // exposes, and are matched to the partition when the split is elaborated.
    std::optional<Expr> lower_split(const std::string& marker, const Continuation& from, const Locals& locals,
                                    unsigned depth) {
        const std::vector<CXCursor>& statements = *from.statements;
        std::size_t position = from.index + 1;
        const std::optional<CXCursor> subject = declared_as(statements[position++], marker + "_subject");
        const CXCursor value = subject ? clang_Cursor_getVarDeclInitializer(*subject) : clang_getNullCursor();
        if (clang_Cursor_isNull(value) != 0) {
            return reject("the subject of this case split was not resolved");
        }
        CaseSplit split;
        split.marker = marker;
        split.operands.push_back(build_expression(value, signature, locals, 0));
        consumed_splits.push_back(Function::SplitSubject{marker, split.operands.front().type});

        // The request that the subject's type be complete computes nothing.
        while (position < statements.size() && clang_getCursorKind(statements[position]) == CXCursor_DeclStmt &&
               std::ranges::all_of(
                   children_of(statements[position]),
                   [](CXCursor declared) { return clang_getCursorKind(declared) == CXCursor_StaticAssert; })) {
            ++position;
        }

        std::map<std::uint32_t, std::uint32_t> labels;
        const std::string label_prefix = marker + "_label_";
        for (; position < statements.size() && clang_getCursorKind(statements[position]) == CXCursor_DeclStmt;
             ++position) {
            const std::vector<CXCursor> declared = children_of(statements[position]);
            const std::string name = declared.size() == 1 ? take(clang_getCursorSpelling(declared[0])) : std::string{};
            const CXCursor label = name.starts_with(label_prefix) ? clang_Cursor_getVarDeclInitializer(declared[0])
                                                                  : clang_getNullCursor();
            const std::string arm = name.substr(std::min(name.size(), label_prefix.size()));
            if (clang_Cursor_isNull(label) != 0 || arm.empty() ||
                arm.find_first_not_of("0123456789") != std::string::npos || arm.size() > 5) {
                return reject("a label of this case split was not resolved");
            }
            labels.emplace(static_cast<std::uint32_t>(std::stoul(arm)),
                           static_cast<std::uint32_t>(split.operands.size()));
            split.operands.push_back(build_expression(label, signature, locals, 0));
        }

        for (std::uint32_t arm = 0; position < statements.size(); ++position, ++arm) {
            if (clang_getCursorKind(statements[position]) != CXCursor_CompoundStmt) {
                return reject("an arm of this case split was not resolved");
            }
            const std::vector<CXCursor> contents = children_of(statements[position]);
            if (contents.empty() || !declared_as(contents[0], marker + "_arm_" + std::to_string(arm))) {
                return reject("an arm of this case split was not resolved");
            }
            // The declarations after the arm's own marker are its binders, in
            // the order they were written; its nested splits and claims are
            // blocks.
            Locals bound = locals;
            std::size_t first = 1;
            std::uint32_t binders = 0;
            for (; first < contents.size() && clang_getCursorKind(contents[first]) == CXCursor_DeclStmt; ++first) {
                const std::vector<CXCursor> declared = children_of(contents[first]);
                if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
                    return reject("a binder of this case split was not resolved");
                }
                Local binder;
                binder.declaration = declared[0];
                binder.type = convert_type(clang_getCursorType(declared[0]), 0, ReferenceModel::Referent);
                binder.spelling = take(clang_getCursorSpelling(declared[0]));
                binder.binder = CaseBinder{marker, arm, binders++};
                // A binder names a value of its own, as in a proof body
                // (SPEC.md CASE-006): it never repeats a parameter's name or an
                // enclosing arm's binder.
                const auto repeats = [&binder](const Local& other) {
                    return other.binder.has_value() && other.spelling == binder.spelling;
                };
                if (std::ranges::any_of(bound, repeats) ||
                    std::ranges::any_of(parameters, [&binder](CXCursor parameter) {
                        return take(clang_getCursorSpelling(parameter)) == binder.spelling;
                    })) {
                    return reject("case binder '" + binder.spelling + "' duplicates an enclosing value name");
                }
                bound.push_back(std::move(binder));
            }
            split.arms.push_back(CaseSplit::Arm{
                labels.contains(arm) ? std::optional<std::uint32_t>{labels.at(arm)} : std::nullopt, binders});
            std::optional<Expr> continued =
                lower_statements(Continuation{from.outer, &contents, first}, bound, depth + 1);
            if (!continued) {
                return std::nullopt;
            }
            split.operands.push_back(std::move(*continued));
        }
        if (split.arms.empty() || labels.size() > split.arms.size() ||
            std::ranges::any_of(labels, [&split](const auto& label) { return label.first >= split.arms.size(); })) {
            return reject("this case split was not resolved");
        }

        Expr result;
        result.type = result_type;
        result.location = presumed_location(clang_getCursorLocation(statements[from.index]));
        result.node = std::move(split);
        return result;
    }

    // The generated declaration a loop invariant was projected into, if the
    // statement is one.
    // A loop clause the projector declared at the head of the body: an
    // `invariant_` condition or a `measure_` expression (SPEC.md 24.1, 24.3).
    struct LoopMarker {
        CXCursor cursor;
        bool measure = false;
    };

    // Whether this statement is the declaration the projector emitted to force
    // a templated function's contract probes to be instantiated alongside it.
    //
    // Every such declaration is generated, so it is recognized by the
    // projector's own prefix, which no ordinary declaration may use.
    [[nodiscard]] bool is_instantiation_marker(CXCursor statement) const {
        if (invariant_prefix.empty() || clang_getCursorKind(statement) != CXCursor_DeclStmt) {
            return false;
        }
        const std::vector<CXCursor> declared = children_of(statement);
        return std::ranges::all_of(
                   declared,
                   [&](CXCursor candidate) {
                       return clang_getCursorKind(candidate) == CXCursor_VarDecl &&
                              take(clang_getCursorSpelling(candidate)).starts_with(invariant_prefix + "force_");
                   }) &&
               !declared.empty();
    }

    [[nodiscard]] std::optional<LoopMarker> invariant_marker(CXCursor statement) const {
        if (invariant_prefix.empty() || clang_getCursorKind(statement) != CXCursor_DeclStmt) {
            return std::nullopt;
        }
        const std::vector<CXCursor> declared = children_of(statement);
        if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
            return std::nullopt;
        }
        const std::string name = take(clang_getCursorSpelling(declared[0]));
        if (name.starts_with(invariant_prefix + "invariant_")) {
            return LoopMarker{declared[0], false};
        }
        if (name.starts_with(invariant_prefix + "measure_")) {
            return LoopMarker{declared[0], true};
        }
        return std::nullopt;
    }

    // A loop, as its entry, its head, one iteration, and what follows it
    // (SPEC.md 24). Every local the loop writes is carried: from the head on it
    // denotes a fresh version, of which only the invariants and the condition
    // are known. A local the loop does not write keeps the version it had.
    std::optional<Expr> lower_loop(const LoopHeader& header, const Locals& locals, unsigned depth) {
        if (depth > kMaxExpressionDepth) {
            return reject("more than " + std::to_string(kMaxExpressionDepth) +
                          " nested or consecutive statements on one path are not modeled");
        }
        std::vector<CXCursor> statements;
        if (clang_getCursorKind(header.body) == CXCursor_CompoundStmt) {
            statements = children_of(header.body);
        } else {
            statements.push_back(header.body);
        }
        std::vector<CXCursor> markers;
        // A lexicographic measure is one marker per component, in the order
        // written (SPEC.md TERMINATION-004).
        std::vector<CXCursor> measure_markers;
        std::size_t first = 0;
        while (first < statements.size()) {
            const std::optional<LoopMarker> marker = invariant_marker(statements[first]);
            if (!marker) {
                break;
            }
            (marker->measure ? measure_markers : markers).push_back(marker->cursor);
            ++first;
        }

        LoopFrame frame;
        frame.id = next_loop++;
        frame.statement = header.statement;
        frame.head = locals;
        frame.increment = header.increment;
        frame.exit = header.exit;
        frame.frames_outside = frames.size();
        frame.condition = header.condition;
        frame.condition_last = header.condition_last;
        std::vector<bool> written(locals.size(), false);
        if (clang_Cursor_isNull(header.condition) == 0) {
            mark_writes(header.condition, locals, written);
        }
        if (header.increment) {
            mark_writes(*header.increment, locals, written);
        }
        mark_writes(header.body, locals, written);
        if (clang_Cursor_isNull(header.condition) == 0) {
            mark_sequence_writes(header.condition, locals, written);
        }
        if (header.increment) {
            mark_sequence_writes(*header.increment, locals, written);
        }
        mark_sequence_writes(header.body, locals, written);
        // An unsafe block in the loop may write whatever it reaches on any
        // iteration, so each such place is carried: at the head it is a fresh
        // value no fact from before the loop describes (SPEC.md LOOP-005). And
        // an iteration, like what follows the loop, may come after the block,
        // so none of them holds a contract's capability.
        const std::vector<CXCursor> unsafe_inside = unsafe_blocks_in(header.body, invariant_prefix);
        if (!unsafe_inside.empty()) {
            for (const std::size_t index : unsafe_reach(locals)) {
                written[index] = true;
            }
        }
        const std::optional<source::SourceLocation> enclosing_revocation = revoked_by;
        if (!unsafe_inside.empty() && !revoked_by.has_value()) {
            if (const std::optional<CXCursor> marker = unsafe_marker_of(unsafe_inside.front(), invariant_prefix)) {
                revoked_by = presumed_location(clang_getCursorLocation(*marker));
            }
        }
        for (std::size_t index = 0; index < locals.size(); ++index) {
            if (written[index]) {
                frame.carried.push_back(index);
                // A container an iteration may reallocate has, at the head, a
                // generation no view formed before the loop was formed at.
                new_generation(frame.head[index],
                               "the loop at " + describe_location(header.statement) + ", which may change it");
            }
        }

        std::vector<Expr> invariants;
        for (const CXCursor marker : markers) {
            const CXCursor initializer = clang_Cursor_getVarDeclInitializer(marker);
            if (clang_Cursor_isNull(initializer) != 0) {
                return reject("a loop invariant was not resolved");
            }
            Expr invariant = build_expression(initializer, signature, frame.head, 0);
            if (!std::holds_alternative<Unsupported>(invariant.node) && invariant.type.kind != TypeKind::Bool) {
                return reject("a loop invariant must be a condition");
            }
            invariants.push_back(std::move(invariant));
            consumed_invariants.push_back(take(clang_getCursorSpelling(marker)));
        }
        // Each measure component is read in the head's scope like an invariant,
        // but it is a value rather than a condition. Its well-founded domain is
        // checked where the obligation is stated (SPEC.md 22.5).
        std::vector<Expr> measures;
        for (const CXCursor marker : measure_markers) {
            const CXCursor initializer = clang_Cursor_getVarDeclInitializer(marker);
            if (clang_Cursor_isNull(initializer) != 0) {
                return reject("a loop measure was not resolved");
            }
            Expr value = build_expression(initializer, signature, frame.head, 0);
            if (!std::holds_alternative<Unsupported>(value.node) && value.type.kind != TypeKind::Int) {
                return reject("a loop measure must be an integer");
            }
            measures.push_back(std::move(value));
            consumed_invariants.push_back(take(clang_getCursorSpelling(marker)));
        }

        frames.push_back(&frame);
        const std::vector<CXCursor> rest(statements.begin() + static_cast<std::ptrdiff_t>(first), statements.end());
        Continuation iteration;
        iteration.iteration = &frame;
        std::optional<Expr> once = lower_statements(Continuation{&iteration, &rest, 0}, frame.head, depth + 1);
        frames.pop_back();
        if (!once) {
            revoked_by = enclosing_revocation;
            return std::nullopt;
        }

        const source::SourceLocation location = presumed_location(clang_getCursorLocation(header.statement));
        Expr head;
        head.type = result_type;
        head.location = location;
        // What happens from the head on. A `do` loop runs its body first and
        // decides at each iteration's end; a `for` without a condition always
        // runs it, and is left only by a `break` or a `return` (SPEC.md
        // LOOP-001). Otherwise the condition decides before each iteration.
        if (header.condition_last || clang_Cursor_isNull(header.condition) != 0) {
            revoked_by = enclosing_revocation;
            if (return_paths(*once) > kMaxReturnPaths) {
                return reject("more than " + std::to_string(kMaxReturnPaths) + " return paths are not modeled");
            }
            head = std::move(*once);
        } else {
            Expr condition = build_expression(header.condition, signature, frame.head, 0);
            std::optional<Expr> after = lower_statements(*header.exit, frame.head, depth + 1);
            revoked_by = enclosing_revocation;
            if (!after) {
                return std::nullopt;
            }
            if (return_paths(*once) + return_paths(*after) > kMaxReturnPaths) {
                return reject("more than " + std::to_string(kMaxReturnPaths) + " return paths are not modeled");
            }
            head.node = Conditional{{std::move(condition), std::move(*once), std::move(*after)}};
        }

        Loop loop;
        loop.loop = frame.id;
        for (const std::size_t index : frame.carried) {
            loop.heads.push_back(frame.head[index].version);
            loop.places.push_back(place_of(locals, index));
            loop.operands.push_back(read_place(locals, index, header.statement));
        }
        loop.invariants = static_cast<std::uint32_t>(invariants.size());
        for (Expr& invariant : invariants) {
            loop.operands.push_back(std::move(invariant));
        }
        loop.measures = static_cast<std::uint32_t>(measures.size());
        for (Expr& measure : measures) {
            loop.operands.push_back(std::move(measure));
        }
        loop.operands.push_back(std::move(head));

        Expr lowered;
        lowered.type = result_type;
        lowered.location = location;
        lowered.node = std::move(loop);
        return lowered;
    }

    // The end of an iteration: the increment, then the next iteration with
    // each carried local at the version it holds here. A local the loop does
    // not carry must still hold its head version, or the scan that decided
    // what the loop carries missed a write.
    std::optional<Expr> end_iteration(const LoopFrame& frame, bool after_increment, const Locals& locals,
                                      unsigned depth) {
        if (frame.increment && !after_increment) {
            Continuation incremented;
            incremented.iteration = &frame;
            incremented.after_increment = true;
            return lower_statement(*frame.increment, incremented, locals, depth + 1);
        }
        if (locals.size() < frame.head.size()) {
            return reject("a loop's locals went out of step with its head");
        }
        Iterate next;
        next.loop = frame.id;
        for (std::size_t index = 0; index < frame.head.size(); ++index) {
            if (clang_equalCursors(locals[index].declaration, frame.head[index].declaration) == 0) {
                return reject("a loop's locals went out of step with its head");
            }
            const bool carried = std::ranges::find(frame.carried, index) != frame.carried.end();
            if (!carried && locals[index].version != frame.head[index].version) {
                return reject("'" + take(clang_getCursorSpelling(locals[index].declaration)) +
                              "' is written inside a loop in a way this implementation does not track");
            }
            if (carried) {
                next.operands.push_back(read_place(locals, index, frame.statement));
            }
        }
        Expr iterated;
        iterated.type = result_type;
        iterated.location = presumed_location(clang_getCursorLocation(frame.statement));
        iterated.node = std::move(next);
        if (!frame.condition_last) {
            return iterated;
        }
        // A `do` loop decides here, where its body ends or a `continue` leaves
        // it, whether another iteration begins; when not, what follows the loop
        // runs under the versions current here and outside the loop.
        Expr condition = build_expression(frame.condition, signature, locals, 0);
        const std::vector<const LoopFrame*> inside = frames;
        frames.resize(frame.frames_outside);
        std::optional<Expr> after = lower_statements(*frame.exit, locals, depth + 1);
        frames = inside;
        if (!after) {
            return std::nullopt;
        }
        Expr decided;
        decided.type = result_type;
        decided.location = iterated.location;
        decided.node = Conditional{{std::move(condition), std::move(iterated), std::move(*after)}};
        return decided;
    }

    // `break` continues with what follows the innermost loop, under the
    // versions current here, and outside that loop.
    std::optional<Expr> lower_break(const Locals& locals, unsigned depth) {
        if (frames.empty()) {
            return reject("'break' outside a modeled loop");
        }
        const LoopFrame& frame = *frames.back();
        const std::vector<const LoopFrame*> inside = frames;
        frames.resize(frame.frames_outside);
        std::optional<Expr> after = lower_statements(*frame.exit, locals, depth + 1);
        frames = inside;
        return after;
    }

    // A branch of the body: what the program does when the condition holds, and
    // what it does when it does not. Each is built on demand because condition
    // elaboration places it on more than one route, and every route needs its
    // own subtree rather than a shared one.
    using Branch = std::function<std::optional<Expr>()>;

    // Elaborate an `if` condition into the routes it selects between.
    //
    // `&&` and `||` state a proposition, and a proposition is not a value: the
    // core computes no Boolean from one (SPEC.md 12.7). They are not lowered as
    // values here either. They are elaborated into the branch structure C++
    // already gives them, which is what makes short-circuit evaluation exact
    // rather than approximated:
    //
    //     if (A && B) T else F   ==>   if (A) { if (B) T else F } else F
    //     if (A || B) T else F   ==>   if (A) T else { if (B) T else F }
    //     if (!A)     T else F   ==>   if (A) F else T
    //
    // `B` appears only under the route on which C++ evaluates it, so no route
    // can state a fact about an operand that did not execute on it. The false
    // route of `A && B` is the union of `!A` and `A && !B`; it is represented as
    // those two routes, never as a single route supposing both operands false.
    // Nesting recurses, so each operand is itself elaborated the same way.
    std::optional<Expr> lower_condition(CXCursor condition, const Branch& when_true, const Branch& when_false,
                                        const Locals& locals, unsigned depth) {
        if (depth > kMaxConditionDepth) {
            return reject("this condition nests more deeply than " + std::to_string(kMaxConditionDepth) + " operators");
        }
        const enum CXCursorKind kind = clang_getCursorKind(condition);
        if (kind == CXCursor_ParenExpr) {
            const auto inner = children_of(condition);
            if (inner.size() == 1)
                return lower_condition(inner[0], when_true, when_false, locals, depth + 1);
        }
        if (kind == CXCursor_UnaryOperator && clang_getCursorUnaryOperatorKind(condition) == CXUnaryOperator_LNot) {
            const auto operands = children_of(condition);
            if (operands.size() == 1)
                return lower_condition(operands[0], when_false, when_true, locals, depth + 1);
        }
        if (kind == CXCursor_BinaryOperator) {
            const enum CXBinaryOperatorKind op = clang_getCursorBinaryOperatorKind(condition);
            const auto operands = children_of(condition);
            if ((op == CXBinaryOperator_LAnd || op == CXBinaryOperator_LOr) && operands.size() == 2) {
                const bool conjunction = op == CXBinaryOperator_LAnd;
                // The second operand is evaluated only on the route the first
                // operand's outcome leads to, which is where it is placed.
                const Branch rest = [&]() -> std::optional<Expr> {
                    return lower_condition(operands[1], when_true, when_false, locals, depth + 1);
                };
                return lower_condition(operands[0], conjunction ? rest : when_true, conjunction ? when_false : rest,
                                       locals, depth + 1);
            }
        }
        Expr value = build_expression(condition, signature, locals, 0);
        std::optional<Expr> taken = when_true();
        if (!taken)
            return std::nullopt;
        std::optional<Expr> untaken = when_false();
        if (!untaken)
            return std::nullopt;
        if (return_paths(*taken) + return_paths(*untaken) > kMaxReturnPaths) {
            return reject("more than " + std::to_string(kMaxReturnPaths) + " return paths are not modeled");
        }
        Expr result;
        result.type = taken->type;
        result.location = value.location;
        result.node = Conditional{{std::move(value), std::move(*taken), std::move(*untaken)}};
        return result;
    }

    std::optional<Expr> lower_branch(CXCursor statement, const std::vector<CXCursor>& parts, const Continuation& next,
                                     const Locals& locals, unsigned depth) {
        const Branch when_true = [&]() -> std::optional<Expr> {
            return lower_statement(parts[1], next, locals, depth + 1);
        };
        const Branch when_false = [&]() -> std::optional<Expr> {
            return parts.size() == 3 ? lower_statement(parts[2], next, locals, depth + 1)
                                     : lower_statements(next, locals, depth + 1);
        };
        std::optional<Expr> result = lower_condition(parts[0], when_true, when_false, locals, depth);
        if (!result)
            return std::nullopt;
        result->location = presumed_location(clang_getCursorLocation(statement));
        return result;
    }

    // An aggregate local, tracked as one place per data member (SPEC.md 12.10).
    //
    // Each member is bound to the value its initializer supplies, at the member's
    // own declared type, so a refined member owes its predicate here exactly as a
    // refined local does. That is what makes `S{-5}` a proof obligation rather
    // than a fact: the crossing happens at construction, where the value is
    // known, instead of being supplied on a later read.
    //
    // Only a form whose construction is fully visible is admitted. Anything else
    // is refused rather than tracked, because an untracked member would read as
    // an unconstrained value while still carrying its declared refinement.
    // One storage leaf of an aggregate's initialization: the path reaching it
    // from the object, the type it was declared with, and the initializer
    // element supplying its first value.
    struct AggregateLeaf {
        std::vector<PlaceStep> path;
        Type type;
        CXCursor initializer;
        std::string spelling;
    };

    // The scalar places an aggregate initializer establishes, in declaration
    // order, following members that are themselves aggregates into their own
    // members (SPEC.md 12.10).
    //
    // A nested member is not one value: it is the places its own members are,
    // reached by a longer path. `s.i.v` and `s.items[0]` are places exactly as
    // `s.a` is, which is why this collects leaves rather than stopping at the
    // first structural member. Returns the reason on refusal.
    std::optional<std::string> collect_leaves(const Type& type, CXCursor initializer, const std::string& written,
                                              const std::vector<PlaceStep>& prefix,
                                              std::vector<AggregateLeaf>& leaves) {
        const auto& components = type.representation.components;
        // `std::array<T, N>` is `N` element places exactly as `T[N]` is
        // (RFC 0020 §3, SPEC.md STDMODEL-011).
        const bool array = type.representation.kind == source::RepresentationKind::Array ||
                           type.representation.kind == source::RepresentationKind::StdArray;
        if (type.representation.kind == source::RepresentationKind::StdArray) {
            library_models.insert(source::RepresentationKind::StdArray);
        }
        if ((type.representation.kind != source::RepresentationKind::Record && !array) || components.empty() ||
            type.projections.size() != components.size()) {
            return "'" + written + "' has type '" + type.spelling + "', which is not modeled";
        }
        if (const std::string& unmodeled = type.representation.rejection; !unmodeled.empty()) {
            return "'" + written + "' has type '" + type.spelling + "', which is not modeled: " + unmodeled;
        }
        if (prefix.size() >= kMaxPlaceDepth) {
            return "'" + written + "' nests deeper than this implementation tracks";
        }
        for (const auto& component : components) {
            if (!component.accessible) {
                return "'" + written + "' has type '" + type.spelling +
                       "' with an inaccessible member, whose construction this body cannot check";
            }
        }
        // Only a form whose effect on every member is visible here can be
        // tracked. Default initialization, a constructor call and any other
        // form leave at least one member holding a value this body cannot
        // state, and a tracked member at an unconstrained value would read as
        // though it held one. That applies at every level, so a nested member
        // needs its own braces rather than an elided initializer.
        if (clang_Cursor_isNull(initializer) != 0 || clang_getCursorKind(initializer) != CXCursor_InitListExpr) {
            return "'" + written + "' of type '" + type.spelling +
                   "' is not initialized by an aggregate initializer, so this body cannot state what each member holds";
        }
        const std::vector<CXCursor> elements = children_of(initializer);
        if (elements.size() != components.size()) {
            return "'" + written + "' of type '" + type.spelling + "' is initialized with " +
                   std::to_string(elements.size()) + " values for " + std::to_string(components.size()) +
                   " members; partial aggregate initialization is not modeled";
        }
        for (std::size_t member = 0; member < components.size(); ++member) {
            if (leaves.size() >= kMaxTrackedLeaves) {
                return "'" + written + "' has more tracked members than the proof resource limit allows";
            }
            const Type& member_type = type.projections[member];
            const std::string member_written =
                array ? written + "[" + components[member].name + "]" : written + "." + components[member].name;
            std::vector<PlaceStep> path = prefix;
            path.push_back(PlaceStep{array ? PlaceStep::Kind::Element : PlaceStep::Kind::Field,
                                     static_cast<std::uint32_t>(member)});
            if (member_type.kind == TypeKind::Value) {
                if (auto refusal = collect_leaves(member_type, elements[member], member_written, path, leaves)) {
                    return refusal;
                }
                continue;
            }
            if (member_type.kind == TypeKind::Unsupported) {
                return "member '" + member_written + "' has type '" + member_type.spelling + "', which is not modeled";
            }
            leaves.push_back(AggregateLeaf{std::move(path), member_type, elements[member], member_written});
        }
        return std::nullopt;
    }

    // The scalar places a value of `type` occupies, in declaration order, with
    // no initializer to supply them.
    //
    // A by-value parameter arrives already holding a value the caller
    // established, so what is enumerated here is where that value lives rather
    // than how it was built -- which is the whole difference from
    // `collect_leaves`. The structural rules are otherwise the same: a member
    // that is itself an aggregate is followed into its own members, and a type
    // this implementation does not model is refused rather than tracked, since
    // an untracked member would read as an unconstrained value while still
    // carrying its declared refinement.
    std::optional<std::string> collect_type_leaves(const Type& type, const std::string& written,
                                                   const std::vector<PlaceStep>& prefix,
                                                   std::vector<AggregateLeaf>& leaves) {
        const auto& components = type.representation.components;
        const bool array = type.representation.kind == source::RepresentationKind::Array ||
                           type.representation.kind == source::RepresentationKind::StdArray;
        if ((type.representation.kind != source::RepresentationKind::Record && !array) || components.empty() ||
            type.projections.size() != components.size()) {
            return "'" + written + "' has type '" + type.spelling + "', which is not modeled";
        }
        // A member the representation could not model is left out of its
        // components, so the components no longer stand at the positions
        // `field_index_of` numbers members by: the place of `s.x` would be
        // tracked under the number an access to the member before it resolves
        // to. Such a type is not tracked at all, rather than tracked with every
        // member after the gap under another member's name.
        if (const std::string& unmodeled = type.representation.rejection; !unmodeled.empty()) {
            return "'" + written + "' has type '" + type.spelling + "', which is not modeled: " + unmodeled;
        }
        if (prefix.size() >= kMaxPlaceDepth) {
            return "'" + written + "' nests deeper than this implementation tracks";
        }
        for (const auto& component : components) {
            if (!component.accessible) {
                return "'" + written + "' has type '" + type.spelling +
                       "' with an inaccessible member, whose value this body cannot state";
            }
        }
        for (std::size_t member = 0; member < components.size(); ++member) {
            if (leaves.size() >= kMaxTrackedLeaves) {
                return "'" + written + "' has more tracked members than the proof resource limit allows";
            }
            const Type& member_type = type.projections[member];
            const std::string member_written =
                array ? written + "[" + components[member].name + "]" : written + "." + components[member].name;
            std::vector<PlaceStep> path = prefix;
            path.push_back(PlaceStep{array ? PlaceStep::Kind::Element : PlaceStep::Kind::Field,
                                     static_cast<std::uint32_t>(member)});
            if (member_type.kind == TypeKind::Value) {
                if (auto refusal = collect_type_leaves(member_type, member_written, path, leaves)) {
                    return refusal;
                }
                continue;
            }
            if (member_type.kind == TypeKind::Unsupported) {
                return "member '" + member_written + "' has type '" + member_type.spelling + "', which is not modeled";
            }
            leaves.push_back(AggregateLeaf{std::move(path), member_type, clang_getNullCursor(), member_written});
        }
        return std::nullopt;
    }

    std::optional<Expr> lower_aggregate(CXCursor declaration, const std::string& name, const Type& type,
                                        const std::vector<CXCursor>& declared, std::size_t index,
                                        const Continuation& next, const Locals& locals, unsigned depth) {
        // An array is a record whose members are its elements, so a constant
        // index names a place exactly as a field name does. A variable index
        // does not: which place it names is not decided here, and deciding it
        // needs the extent obligation the capability model supplies.
        std::vector<AggregateLeaf> leaves;
        if (auto refusal = collect_leaves(type, clang_Cursor_getVarDeclInitializer(declaration), name, {}, leaves)) {
            return reject("local " + *refusal);
        }

        Locals declaring = locals;
        std::vector<std::uint32_t> versions;
        std::vector<Expr> values;
        for (const AggregateLeaf& leaf : leaves) {
            std::vector<std::size_t> invalidated;
            auto evaluated = evaluate(leaf.initializer, declaring, invalidated);
            if (!evaluated)
                return std::nullopt;
            if (!invalidated.empty())
                return reject("initializing '" + leaf.spelling +
                              "' has uncertain aliases; use a separate call "
                              "statement");
            if (!std::holds_alternative<Unsupported>(evaluated->node) &&
                !same_modeled_value(leaf.type, evaluated->type)) {
                return reject("initializing '" + leaf.spelling + "' of type '" + leaf.type.spelling + "' from '" +
                              evaluated->type.spelling + "' is a conversion that is not modeled");
            }
            versions.push_back(next_version++);
            values.push_back(std::move(*evaluated));
            declaring.push_back(Local{.declaration = declaration,
                                      .version = versions.back(),
                                      .type = leaf.type,
                                      .path = leaf.path,
                                      .spelling = leaf.spelling});
        }

        std::optional<Expr> body = lower_declaration(declared, index + 1, next, declaring, depth);
        if (!body)
            return std::nullopt;
        // Innermost member last, so each member's version is established before
        // the body that reads it and the version order matches the binding order.
        for (std::size_t leaf = leaves.size(); leaf > 0; --leaf) {
            body = bind(versions[leaf - 1], place_of(declaring, locals.size() + leaf - 1), std::move(values[leaf - 1]),
                        std::move(*body), declaration, leaves[leaf - 1].type);
        }
        return body;
    }

    // Ghost state declared here (SPEC.md 25): each variable the declaration
    // after the marker declares is a proof-only value, stated as a term over the
    // versions current here. Its initializer never runs, so it is read the way a
    // specification expression is, and nothing it names is written. Whether the
    // declaration and every use of it are admissible was decided before the body
    // was lowered (`scan_ghost_state`).
    std::optional<Expr> lower_ghost(const Continuation& from, const Locals& locals, unsigned depth) {
        const std::vector<CXCursor>& statements = *from.statements;
        if (from.index + 1 >= statements.size() ||
            clang_getCursorKind(statements[from.index + 1]) != CXCursor_DeclStmt) {
            return reject("a ghost declaration was not resolved");
        }
        return lower_ghost_declaration(children_of(statements[from.index + 1]), 0,
                                       Continuation{from.outer, from.statements, from.index + 2}, locals, depth);
    }

    std::optional<Expr> lower_ghost_declaration(const std::vector<CXCursor>& declared, std::size_t index,
                                                const Continuation& next, const Locals& locals, unsigned depth) {
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
                return reject(resolved.error());
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
            return reject("initializing ghost '" + name + "' of type '" + type.spelling + "' from '" +
                          value.type.spelling + "' is a conversion that is not modeled");
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

    [[nodiscard]] static std::optional<CXCursor> moved_operand(CXCursor cursor) {
        return moved_operand_of(cursor);
    }

    // Why the elements of `source` are not known to satisfy what `target`'s
    // element type requires, if they are not: a copy or move carries the
    // values, never a proof they meet a refinement the source never owed.
    [[nodiscard]] static std::optional<std::string> refinement_gap(const Local& target, const Local& source) {
        if (!target.sequence.has_value() || !source.sequence.has_value()) {
            return "'" + source.spelling + "' and '" + target.spelling + "' are not both containers this body tracks";
        }
        const std::vector<Refinement>& held = source.sequence->element.refinements;
        for (const Refinement& refinement : target.sequence->element.refinements) {
            if (std::ranges::find(held, refinement) == held.end()) {
                return "the elements of '" + source.spelling + "' are not known to satisfy '" + refinement.name +
                       "', which the elements of '" + target.spelling + "' require";
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] Expr read_root(const Locals& state, std::size_t root, CXCursor at) const {
        return read_place(state, root, at);
    }

    // A vector, a string or a span local (RFC 0020 §3, §6): the root of a
    // modeled sequence, established by one of the modeled constructors as a
    // trusted library summary whose one fact is the length.
    //
    // A value the construction puts into an element is a refinement crossing
    // into the element type, owed where it enters (SPEC.md 17.2): a listed
    // element, a fill value, or the value-initialized element of a sized one. A
    // listed element is also bound to its place, so `v[0]` after `{1, 2}` is 1.
    std::optional<Expr> lower_sequence_declaration(CXCursor declaration, const std::string& name, const Type& type,
                                                   const std::vector<CXCursor>& declared, std::size_t index,
                                                   const Continuation& next, const Locals& locals, unsigned depth) {
        using K = source::RepresentationKind;
        const K family = type.representation.kind;
        if (!type.representation.rejection.empty() || type.projections.size() != 1) {
            return reject("local '" + name + "' has type '" + type.spelling +
                          "', which is not modeled: " + type.representation.rejection);
        }
        library_models.insert(family);
        const CXCursor written_initializer = clang_Cursor_getVarDeclInitializer(declaration);
        if (clang_Cursor_isNull(written_initializer) != 0) {
            return reject("local '" + name + "' is declared without an initializer, so it holds no modeled value");
        }
        const std::optional<SequenceCall> constructed = sequence_call(strip_parens(written_initializer));
        if (!constructed || !constructed->constructor) {
            return reject("local '" + name + "' of type '" + type.spelling +
                          "' is not initialized by a modeled constructor (SPEC.md STDMODEL-013)");
        }
        const Type& length = type.projections.front();
        const auto count_literal = [&](std::int64_t value) {
            Expr literal;
            literal.type = length;
            literal.location = presumed_location(clang_getCursorLocation(written_initializer));
            literal.node = IntLiteral{value};
            return literal;
        };
        const std::vector<CXCursor> formals = parameters_of(constructed->method);
        const auto formal = [&](std::size_t position) {
            return position < formals.size() ? clang_getCanonicalType(clang_getCursorType(formals[position]))
                                             : CXType{CXType_Invalid, {nullptr, nullptr}};
        };
        const auto of_this_class = [&](CXType reference) {
            return clang_equalCursors(
                       clang_getTypeDeclaration(clang_getCanonicalType(clang_getPointeeType(reference))),
                       clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(declaration)))) != 0;
        };

        Locals declaring = locals;
        Local root{.declaration = declaration, .type = type, .spelling = name};
        Local::Sequence& held = root.sequence.emplace();
        held.kind = family;
        source::LibraryOperation operation = source::LibraryOperation::Construct;
        std::vector<Expr> arguments;
        std::vector<CallEffect> effects;
        std::vector<std::pair<Expr, Type>> charged;
        std::vector<Expr> listed;
        std::vector<std::size_t> invalidated;
        const std::size_t count = constructed->arguments.size();

        if (family == K::Span) {
            // A span local views a whole vector or string this body tracks, and
            // is usable while that storage's generation stands (STDMODEL-015).
            const std::optional<std::size_t> viewed =
                count == 1 ? owning_root(constructed->arguments.front(), declaring) : std::nullopt;
            const std::optional<Local::Sequence>* viewed_sequence =
                viewed.has_value() ? &declaring[*viewed].sequence : nullptr;
            if (!viewed || viewed_sequence == nullptr || !viewed_sequence->has_value()) {
                return reject("span '" + name +
                              "' is modeled only as a view of a whole vector or string this body tracks "
                              "(SPEC.md STDMODEL-014)");
            }
            operation = source::LibraryOperation::ViewOf;
            held.element = (*viewed_sequence)->element;
            held.external_elements = (*viewed_sequence)->external_elements;
            held.views = viewed;
            root.borrows = Local::Generation{*viewed, declaring[*viewed].version};
            arguments.push_back(read_root(declaring, *viewed, written_initializer));
        } else {
            auto element = sequence_element(declaration, clang_getCursorType(declaration), refinements);
            if (!element) {
                return reject("local '" + name + "': " + element.error());
            }
            held.element = std::move(*element);
            const Type& element_type = held.element;
            const CXType first = formal(0);
            if (count == 0) {
                arguments.push_back(count_literal(0));
            } else if (count == 1 && is_standard_template(first, "initializer_list")) {
                CXCursor list = constructed->arguments.front();
                for (unsigned step = 0;
                     step < kMaxExpressionDepth && clang_getCursorKind(list) != CXCursor_InitListExpr; ++step) {
                    const std::vector<CXCursor> inner = children_of(list);
                    if (inner.size() != 1) {
                        break;
                    }
                    list = inner.front();
                }
                if (clang_getCursorKind(list) != CXCursor_InitListExpr) {
                    return reject("the initializer list of '" + name + "' was not resolved");
                }
                for (const CXCursor item : children_of(list)) {
                    auto value = evaluate(item, declaring, invalidated);
                    if (!value) {
                        return std::nullopt;
                    }
                    if (!invalidated.empty()) {
                        return reject("an element of '" + name +
                                      "' is computed by a call with effects; call it in a "
                                      "statement of its own");
                    }
                    if (!std::holds_alternative<Unsupported>(value->node) &&
                        !same_modeled_value(element_type, value->type)) {
                        return reject("an element of '" + name + "' of type '" + element_type.spelling + "' is '" +
                                      value->type.spelling + "', a conversion that is not modeled");
                    }
                    listed.push_back(std::move(*value));
                }
                if (listed.size() > kMaxTrackedLeaves) {
                    return reject("'" + name + "' lists more elements than the proof resource limit allows");
                }
                arguments.push_back(count_literal(static_cast<std::int64_t>(listed.size())));
            } else if ((count == 1 || count == 2) && convert_type(first).kind == TypeKind::Int &&
                       (count != 2 || family != K::String)) {
                // `S v(n)` holds `n` value-initialized elements; `S v(n, x)` holds
                // `n` copies of `x`. Either value enters the element type.
                if (!materialize(constructed->arguments[0], declaring)) {
                    return std::nullopt;
                }
                Expr size = build_expression(constructed->arguments[0], signature, declaring, 0);
                if (!std::holds_alternative<Unsupported>(size.node) && !same_modeled_value(length, size.type)) {
                    return reject("the length of '" + name + "' is '" + size.type.spelling +
                                  "', a conversion to its size type that is not modeled");
                }
                arguments.push_back(std::move(size));
                if (count == 2) {
                    if (!materialize(constructed->arguments[1], declaring)) {
                        return std::nullopt;
                    }
                    Expr fill = build_expression(constructed->arguments[1], signature, declaring, 0);
                    if (!std::holds_alternative<Unsupported>(fill.node) &&
                        !same_modeled_value(element_type, fill.type)) {
                        return reject("the fill value of '" + name + "' is a conversion that is not modeled");
                    }
                    charged.emplace_back(std::move(fill), element_type);
                } else if (element_type.kind == TypeKind::Int || element_type.kind == TypeKind::Bool) {
                    Expr zero;
                    zero.type = element_type;
                    zero.location = presumed_location(clang_getCursorLocation(written_initializer));
                    zero.node = IntLiteral{0};
                    charged.emplace_back(std::move(zero), element_type);
                }
            } else if (family == K::String && count == 1 &&
                       clang_getCursorKind(strip_parens(constructed->arguments.front())) == CXCursor_StringLiteral) {
                // `basic_string(const char*)` takes the characters up to the
                // first null, which is exactly what Clang's evaluation of the
                // literal as a C string yields.
                // Clang evaluates the pointer the literal decays to as the
                // literal it points at.
                CXEvalResult evaluated = clang_Cursor_Evaluate(constructed->arguments.front());
                if (evaluated == nullptr) {
                    return reject("the string literal initializing '" + name + "' could not be evaluated");
                }
                const bool text = clang_EvalResult_getKind(evaluated) == CXEval_StrLiteral;
                const std::size_t characters = text ? std::string_view(clang_EvalResult_getAsStr(evaluated)).size() : 0;
                clang_EvalResult_dispose(evaluated);
                if (!text) {
                    return reject("the string literal initializing '" + name + "' could not be evaluated");
                }
                arguments.push_back(count_literal(static_cast<std::int64_t>(characters)));
            } else if (count == 1 && (first.kind == CXType_LValueReference || first.kind == CXType_RValueReference) &&
                       of_this_class(first)) {
                const bool moving = first.kind == CXType_RValueReference;
                const std::optional<CXCursor> operand =
                    moving ? moved_operand(constructed->arguments.front()) : constructed->arguments.front();
                const std::optional<std::size_t> origin =
                    operand ? owning_root(*operand, declaring) : std::optional<std::size_t>{};
                if (!origin) {
                    return reject("'" + name + "' is " + (moving ? "moved" : "copied") +
                                  " from something other than a container this body tracks");
                }
                if (auto gap = refinement_gap(root, declaring[*origin])) {
                    return reject(std::move(*gap));
                }
                arguments.push_back(read_root(declaring, *origin, written_initializer));
                if (moving) {
                    // A move takes the source's storage: every view of it and
                    // every fact about it end here (SPEC.md STORAGE-008,
                    // STDMODEL-015, STDMODEL-021). A source that is caller storage could be
                    // what a capability designates, so it is not moved from.
                    if (declaring[*origin].external) {
                        return reject("'" + name + "' is moved from '" + declaring[*origin].spelling +
                                      "', which is caller storage; only a container this body owns is moved from");
                    }
                    operation = source::LibraryOperation::Move;
                    arguments.push_back(read_root(declaring, *origin, written_initializer));
                    effects.push_back(new_generation(declaring[*origin],
                                                     "being moved from at " + describe_location(written_initializer)));
                    invalidated = invalidate_aliases(*origin, declaring);
                } else {
                    operation = source::LibraryOperation::Copy;
                }
            } else {
                return reject("this constructor of '" + type.spelling + "' is not modeled (SPEC.md STDMODEL-013)");
            }
        }

        // Versions are numbered in evaluation order: what enters an element
        // first, then the constructed value, then the listed elements.
        std::vector<std::uint32_t> charged_versions;
        charged_versions.reserve(charged.size());
        for (std::size_t position = 0; position < charged.size(); ++position) {
            charged_versions.push_back(next_version++);
        }
        // The root: the constructed value, stated by the summary.
        Expr constructed_value =
            library_call({family, operation}, type, library_name(family, std::string(source::describe(operation))),
                         std::move(arguments), type, written_initializer);
        std::get<Call>(constructed_value.node).effects = std::move(effects);
        root.version = next_version++;
        const std::size_t root_index = declaring.size();
        declaring.push_back(root);

        // Each listed element is the place `v[j]` names at this generation.
        std::vector<std::uint32_t> element_versions;
        for (std::size_t position = 0; position < listed.size(); ++position) {
            Local element;
            element.declaration = declaration;
            element.version = next_version++;
            element.type = held.element;
            element.path = {PlaceStep{PlaceStep::Kind::Element, static_cast<std::uint32_t>(position), 0}};
            element.spelling = name + "[" + std::to_string(position) + "]";
            element.symbolic = true;
            element.index_value.push_back(count_literal(static_cast<std::int64_t>(position)));
            Expr extent;
            extent.type = length;
            extent.location = presumed_location(clang_getCursorLocation(declaration));
            extent.node = Projection{0, {read_root(declaring, root_index, declaration)}};
            element.extent.push_back(std::move(extent));
            element.formed_at = Local::Generation{root_index, root.version};
            element_versions.push_back(element.version);
            declaring.push_back(std::move(element));
        }

        std::optional<Expr> body = lower_declaration(declared, index + 1, next, declaring, depth);
        if (!body) {
            return std::nullopt;
        }
        for (std::size_t position = listed.size(); position > 0; --position) {
            body = bind(element_versions[position - 1], place_of(declaring, root_index + position),
                        std::move(listed[position - 1]), std::move(*body), declaration, held.element);
        }
        for (const std::size_t changed : std::views::reverse(invalidated)) {
            *body = unknown(declaring, changed, std::move(*body), declaration);
        }
        body = bind(root.version, place_of(declaring, root_index), std::move(constructed_value), std::move(*body),
                    declaration, type);
        // A value entering an element owes the element type's refinement where
        // it enters, before the construction that stores it.
        for (std::size_t position = charged.size(); position > 0; --position) {
            body = bind(charged_versions[position - 1], anonymous_place("element of " + name),
                        std::move(charged[position - 1].first), std::move(*body), declaration,
                        charged[position - 1].second);
        }
        return body;
    }

    std::optional<Expr> lower_declaration(const std::vector<CXCursor>& declared, std::size_t index,
                                          const Continuation& next, const Locals& locals, unsigned depth) {
        if (index == declared.size()) {
            return lower_statements(next, locals, depth + 1);
        }
        const CXCursor declaration = declared[index];
        const std::string name = take(clang_getCursorSpelling(declaration));
        if (clang_getCursorKind(declaration) != CXCursor_VarDecl) {
            return reject("only variable declarations are modeled inside a verified body; found '" +
                          take(clang_getCursorKindSpelling(clang_getCursorKind(declaration))) + "'");
        }
        // An invariant the loop lowering did not take is never read as a
        // statement of the body: that would drop it without a word. Nor is a
        // contradiction's block read anywhere but where it opens.
        if (!invariant_prefix.empty() && name.starts_with(invariant_prefix + "contradiction_")) {
            return reject("a claim that a path cannot occur was not read where it was written");
        }
        if (!invariant_prefix.empty() && name.starts_with(invariant_prefix)) {
            return reject("a loop invariant is attached only to a while or for loop whose body is a block");
        }
        const enum CX_StorageClass storage = clang_Cursor_getStorageClass(declaration);
        if (storage != CX_SC_None && storage != CX_SC_Auto) {
            return reject("local '" + name + "' does not have automatic storage");
        }
        if (clang_getCursorTLSKind(declaration) != CXTLS_None) {
            return reject("thread-local '" + name + "' is not modeled");
        }
        const CXType written = clang_getCursorType(declaration);
        const auto canonical = clang_getCanonicalType(written);
        const bool reference = canonical.kind == CXType_LValueReference || canonical.kind == CXType_RValueReference;
        const CXType value_type = reference ? reference_value_type(written) : written;
        Type type = convert_type(value_type, 0, ReferenceModel::Opaque, refinements);
        // A vector, a string or a span local is the root of a modeled sequence,
        // whose versions carry its length (RFC 0020 §3).
        if (!reference && source::is_sequence(type.representation.kind)) {
            return lower_sequence_declaration(declaration, name, type, declared, index, next, locals, depth);
        }
        // A verified body states a local as one modeled value under logical
        // versioning. A structural value has components rather than such a
        // value, so an aggregate local is tracked as one place per member
        // instead (SPEC.md 12.10): each member is storage of its own, with its
        // own version, and writing one leaves the others alone.
        if (type.kind == TypeKind::Value && !reference) {
            return lower_aggregate(declaration, name, type, declared, index, next, locals, depth);
        }
        if (type.kind == TypeKind::Unsupported || type.kind == TypeKind::Value) {
            return reject("local '" + name + "' has type '" + type.spelling + "', which is not modeled");
        }
        if (refinements != nullptr) {
            auto resolved = refinements_of(declaration, value_type, *refinements);
            if (!resolved)
                return reject(resolved.error());
            type.refinements = std::move(*resolved);
        }
        CXCursor initializer = clang_Cursor_getVarDeclInitializer(declaration);
        if (clang_Cursor_isNull(initializer) != 0) {
            return reject("local '" + name + "' is declared without an initializer, so it holds no modeled value");
        }
        std::optional<std::size_t> referent;
        std::optional<Local::Generation> borrows;
        Locals declaring = locals;
        if (reference && is_sequence_subscript(initializer)) {
            // A reference to a container element is bound to the element place
            // at the current generation, and is usable only while that
            // generation stands (RFC 0020 §4, STDMODEL-015). An element of a
            // span parameter is reached only under a capability an unsafe
            // block can revoke, which a reference could outlive, so it is not
            // bound.
            const bool constant = clang_isConstQualifiedType(clang_getPointeeType(canonical)) != 0;
            const std::size_t before = declaring.size();
            referent = resolve_sequence_element(initializer, declaring,
                                                constant ? Capability::Kind::Readable : Capability::Kind::Writable);
            if (!referent) {
                return std::nullopt;
            }
            for (std::size_t formed = before; formed < declaring.size(); ++formed) {
                formed_derefs.push_back(declaring[formed]);
            }
            if (!declaring[*referent].formed_at.has_value()) {
                return reject("reference '" + name +
                              "' binds an element of a span parameter; a reference is bound only to an element of a "
                              "container this body tracks");
            }
            borrows = declaring[*referent].formed_at;
            if (!same_modeled_value(type, declaring[*referent].type))
                return reject("reference binding changes the modeled value type");
        } else if (reference) {
            // A reference denotes existing storage (SPEC.md 12.9), so it binds
            // whatever place its initializer names, through the one access
            // resolver: a local, a member, an element, or a member of one.
            referent = tracked_place(initializer, locals, signature);
            if (!referent) {
                return reject("reference '" + name +
                              "' must bind a tracked local object; this reference binding is not modeled");
            }
            if (!same_modeled_value(type, locals[*referent].type))
                return reject("reference binding changes the modeled value type");
        }
        if (clang_getCursorKind(initializer) == CXCursor_InitListExpr) {
            const std::vector<CXCursor> elements = children_of(initializer);
            if (elements.size() != 1) {
                return reject("the initializer of '" + name + "' is not a single modeled value");
            }
            initializer = elements[0];
        }
        std::vector<std::size_t> invalidated;
        auto evaluated = evaluate(initializer, declaring, invalidated);
        if (!evaluated)
            return std::nullopt;
        Expr value = std::move(*evaluated);
        if (!std::holds_alternative<Unsupported>(value.node) && !same_modeled_value(type, value.type)) {
            return reject("initializing '" + name + "' of type '" + type.spelling + "' from '" + value.type.spelling +
                          "' is a conversion that is not modeled");
        }
        const std::uint32_t version = next_version++;
        declaring.push_back(Local{
            .declaration = declaration, .version = version, .type = type, .referent = referent, .spelling = name});
        declaring.back().borrows = borrows;
        std::optional<Expr> body = lower_declaration(declared, index + 1, next, declaring, depth);
        if (!body) {
            return std::nullopt;
        }
        for (auto changed : invalidated)
            *body = unknown(declaring, changed, std::move(*body), declaration);
        return bind(version, place_of(declaring, declaring.size() - 1), std::move(value), std::move(*body), declaration,
                    type);
    }

    // The place a write targets, resolved the same way a read is.
    //
    // Every write form - a local, a member, an element, a member of a member -
    // resolves through the one access resolver, so a write reaches exactly the
    // place written and leaves every place disjoint from it alone (SPEC.md
    // 12.10). Only storage this body tracks is ever written.
    std::optional<std::size_t> written_local(CXCursor target, Locals& locals) {
        target = strip_parens(target);
        // An element of a vector, a string or a span is written through its
        // element place at the current generation, owing its bound and the
        // element type's refinement (RFC 0020 §3, §6).
        if (is_sequence_subscript(target)) {
            const std::size_t before = locals.size();
            const auto element = resolve_sequence_element(target, locals, Capability::Kind::Writable);
            for (std::size_t index = before; index < locals.size(); ++index) {
                formed_derefs.push_back(locals[index]);
            }
            return element;
        }
        const auto access = resolve_access(target);
        // A write through a pointer is a write to the pointee place, and owes
        // `writable` there. `readable` does not suffice: an output buffer may
        // be writable and not readable, and a readable one may not be written
        // (RFC 0014 §3, SPEC.md VERIFIED-038).
        if (access && access->dereferenced) {
            const std::size_t before = locals.size();
            const auto storage = resolve_storage(target, locals, Capability::Kind::Writable);
            if (!storage) {
                return rejection.empty() ? reject("writing through a pointer requires a memory capability this "
                                                  "implementation could not resolve")
                                         : std::nullopt;
            }
            // A pointee written for the first time still needs an entry value:
            // the write establishes the next version, and the version before it
            // must exist for that to be well formed.
            for (std::size_t index = before; index < locals.size(); ++index) {
                if (locals[index].is_deref()) {
                    formed_derefs.push_back(locals[index]);
                }
            }
            return storage;
        }
        // A symbolic subscript is written through the same place machinery as
        // any other element: the index owes its bound, and the write reaches
        // every element that may be the one selected.
        if (access && !access->symbolic_indices.empty()) {
            const std::size_t before = locals.size();
            const auto storage = resolve_symbolic_element(locals, *access);
            if (!storage) {
                return rejection.empty() ? reject("this subscript does not name tracked storage") : std::nullopt;
            }
            for (std::size_t index = before; index < locals.size(); ++index) {
                if (locals[index].symbolic) {
                    formed_derefs.push_back(locals[index]);
                }
            }
            return storage;
        }
        if (!access) {
            if (clang_getCursorKind(target) == CXCursor_ArraySubscriptExpr) {
                return reject("this subscript does not name one tracked element: writing through a variable index "
                              "requires the extent obligations of RFC 0014, which are not implemented");
            }
            if (clang_getCursorKind(target) == CXCursor_MemberRefExpr) {
                return reject("this member's object is not tracked storage of this body, so writing it has no modeled "
                              "effect");
            }
            return reject("only a local variable is assigned in a modeled body");
        }
        const CXCursor declaration = access->declaration;
        const std::string name = take(clang_getCursorSpelling(declaration));
        const std::optional<std::size_t> local = find_binding(locals, declaration, access->path);
        if (local && locals[locals[*local].referent.value_or(*local)].read_only) {
            return reject("parameter '" + name +
                          "' has no modeled writable storage: the object it designates is "
                          "read here, and its post-state is not stated member by member");
        }
        if (!local) {
            if (!access->path.empty()) {
                return reject("this member's object is not tracked storage of this body, so writing it has no modeled "
                              "effect");
            }
            if (clang_getCursorKind(declaration) == CXCursor_ParmDecl) {
                return reject("parameter '" + name + "' has no modeled writable storage");
            }
            return reject("'" + name + "' is not a local of this body");
        }
        // A reference to a container element is written only while the storage
        // it was bound to is unchanged (STDMODEL-015).
        if (std::optional<std::string> stale = stale_borrow(locals, *local)) {
            return reject(std::move(*stale));
        }
        return local;
    }

    std::optional<Expr> write(std::size_t local, Expr value, CXCursor statement, const Continuation& next,
                              const Locals& locals, unsigned depth) {
        const std::uint32_t version = next_version++;
        Locals assigned = locals;
        const std::size_t storage = locals[local].referent.value_or(local);
        assigned[storage].version = version;
        const auto invalidated = invalidate_aliases(storage, assigned);
        std::optional<Expr> body = lower_statements(next, assigned, depth + 1);
        if (!body) {
            return std::nullopt;
        }
        // The version an assignment establishes is a value entering the local's
        // declared type exactly as the declaration's was, so it carries the same
        // type - refinement and all. Dropping it here would let a write into a
        // refined local escape the obligation its declaration owed (SPEC.md 17.2).
        Type required = locals[storage].type;
        auto require = [&](const Type& type) {
            for (const auto& refinement : type.refinements)
                if (std::ranges::find(required.refinements, refinement) == required.refinements.end())
                    required.refinements.push_back(refinement);
        };
        require(locals[local].type);
        for (const auto index : invalidated) {
            require(locals[index].type);
            *body = unknown(assigned, index, std::move(*body), statement);
        }
        return bind(version, place_of(locals, local), std::move(value), std::move(*body), statement, required);
    }

    std::optional<Expr> lower_assignment(CXCursor statement, const Continuation& next, const Locals& locals,
                                         unsigned depth) {
        const std::vector<CXCursor> operands = children_of(statement);
        if (operands.size() != 2) {
            return reject("an assignment requires a target and a value");
        }
        Locals state = locals;
        const std::optional<std::size_t> local = written_local(operands[0], state);
        if (!local) {
            return std::nullopt;
        }
        const Type type = state[*local].type;
        std::vector<std::size_t> invalidated;
        auto evaluated = evaluate(operands[1], state, invalidated);
        if (!evaluated)
            return std::nullopt;
        Expr value = std::move(*evaluated);
        if (!invalidated.empty())
            return reject("assignment call has uncertain aliases; use a separate call statement");
        // C++ evaluates the assigned value before the place it is assigned to
        // (C++17 [expr.ass]). A call there that may replace a container's storage
        // leaves an element or an element reference resolved on the left
        // designating storage that may be gone (SPEC.md STDMODEL-015).
        if (std::optional<std::string> stale = stale_borrow(state, *local)) {
            return reject(std::move(*stale));
        }
        if (!generation_current(state, state[*local])) {
            return reject("the value assigned to this container element is computed by a call that may replace the "
                          "container's storage; make that call a statement of its own");
        }
        if (!std::holds_alternative<Unsupported>(value.node) && !same_modeled_value(type, value.type)) {
            return reject("assigning '" + value.type.spelling + "' to '" + state[*local].spelling + "' of type '" +
                          type.spelling + "' is a conversion that is not modeled");
        }
        return write(*local, std::move(value), statement, next, state, depth);
    }

    // `x += e`, `x -= e`, `x *= e`, `x /= e`, `x %= e`, `++x`, `x++`, `--x` and
    // `x--` as statements. Each is the assignment `x = x op e` (or `x op 1`) at
    // the local's own type, which C++ guarantees exactly when that type is not
    // promoted first and `e` is of that type after its own conversions; the
    // arithmetic then owes what it owes anywhere (SPEC.md ARITH-013, 12.8).
    std::optional<Expr> lower_update(CXCursor statement, const Continuation& next, const Locals& locals,
                                     unsigned depth) {
        const CXCursorKind kind = clang_getCursorKind(statement);
        const std::vector<CXCursor> operands = children_of(statement);
        BinaryOp op = BinaryOp::Unsupported;
        if (kind == CXCursor_CompoundAssignOperator) {
            const enum CXBinaryOperatorKind written = clang_getCursorBinaryOperatorKind(statement);
            if (written == CXBinaryOperator_AddAssign) {
                op = BinaryOp::Add;
            } else if (written == CXBinaryOperator_SubAssign) {
                op = BinaryOp::Sub;
            } else if (written == CXBinaryOperator_MulAssign) {
                op = BinaryOp::Mul;
            } else if (written == CXBinaryOperator_DivAssign) {
                op = BinaryOp::Div;
            } else if (written == CXBinaryOperator_RemAssign) {
                op = BinaryOp::Rem;
            } else {
                return reject("compound assignment '" + take(clang_getBinaryOperatorKindSpelling(written)) +
                              "' is not modeled");
            }
            if (operands.size() != 2) {
                return reject("a compound assignment requires a target and a value");
            }
        } else {
            const enum CXUnaryOperatorKind written = clang_getCursorUnaryOperatorKind(statement);
            if (written == CXUnaryOperator_PreInc || written == CXUnaryOperator_PostInc) {
                op = BinaryOp::Add;
            } else if (written == CXUnaryOperator_PreDec || written == CXUnaryOperator_PostDec) {
                op = BinaryOp::Sub;
            } else {
                return reject(
                    unmodeled_statement("operator '" + take(clang_getUnaryOperatorKindSpelling(written)) + "'"));
            }
            if (operands.size() != 1) {
                return reject("an increment or decrement requires one operand");
            }
        }

        Locals state = locals;
        const bool element = is_sequence_subscript(operands[0]);
        // A compound update reads the place and then writes it, so it owes both
        // capabilities. Neither entails the other, so both are required
        // explicitly (RFC 0014 §3). A container element read here is formed at
        // the current generation and bound before the statement, like any
        // element the statement reads (RFC 0020 §3).
        if (element) {
            const std::size_t before = state.size();
            if (!resolve_sequence_element(operands[0], state, Capability::Kind::Readable)) {
                return std::nullopt;
            }
            for (std::size_t formed = before; formed < state.size(); ++formed) {
                formed_derefs.push_back(state[formed]);
            }
        } else if (const auto access = resolve_access(strip_parens(operands[0])); access && access->dereferenced) {
            if (!resolve_storage(strip_parens(operands[0]), state, Capability::Kind::Readable)) {
                return rejection.empty() ? reject("updating through a pointer requires a readable capability")
                                         : std::nullopt;
            }
        }
        const std::optional<std::size_t> local = written_local(operands[0], state);
        if (!local) {
            return std::nullopt;
        }
        const Local target = state[*local];
        const std::string name = target.spelling;
        // The promotion question is about the storage being updated, which for a
        // member is the member's own type, not its object's, and for a container
        // element is the element's.
        if (promoted_before_arithmetic(element ? clang_getCursorType(strip_parens(operands[0]))
                                               : clang_getCursorType(target.path.empty()
                                                                         ? target.declaration
                                                                         : clang_getCursorReferenced(operands[0])))) {
            return reject("updating '" + name + "' of type '" + target.type.spelling +
                          "' computes in 'int' after promotion and converts back, which is not modeled");
        }

        // The update reads the place it writes, through the one read path: a
        // compound assignment is `x = x op e` at the same storage.
        Expr current = read_place(state, target.referent.value_or(*local), operands[0]);

        Expr amount;
        if (operands.size() == 2) {
            if (!materialize(operands[1], state)) {
                return std::nullopt;
            }
            amount = build_expression(operands[1], signature, state, 0);
            if (!std::holds_alternative<Unsupported>(amount.node) && !same_modeled_value(target.type, amount.type)) {
                return reject("updating '" + name + "' of type '" + target.type.spelling + "' by '" +
                              amount.type.spelling + "' is a conversion that is not modeled");
            }
        } else {
            amount.type = target.type;
            amount.location = presumed_location(clang_getCursorLocation(statement));
            amount.node = IntLiteral{1};
        }

        Expr value;
        value.type = target.type;
        value.location = presumed_location(clang_getCursorLocation(statement));
        value.node = Binary{op, {std::move(current), std::move(amount)}};
        return write(*local, std::move(value), statement, next, state, depth);
    }
};

void extract_body(Function& function, CXCursor cursor, const Signature& signature, const std::string& invariant_prefix,
                  const std::vector<Selection::Refinement>& refinements, bool executable_state,
                  const std::vector<StatedCapability>* capabilities) {
    const std::vector<CXCursor>& parameters = signature.parameters;
    const std::vector<CXCursor> members = children_of(cursor);

    std::size_t body_index = members.size();
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (clang_getCursorKind(members[index]) == CXCursor_CompoundStmt) {
            body_index = index;
        }
    }
    if (body_index == members.size()) {
        function.has_body = false;
        return;
    }

    function.has_body = true;

    const std::vector<CXCursor> statements = children_of(members[body_index]);
    BodyLowering lowering{.signature = signature,
                          .parameters = parameters,
                          .result_type = function.result,
                          .invariant_prefix = invariant_prefix,
                          .refinements = &refinements,
                          .executable_state = executable_state,
                          .capabilities = capabilities};
    lowering.completion_location = presumed_location(clang_getRangeEnd(clang_getCursorExtent(members[body_index])));
    lowering.escaped = escaped_locals(members[body_index]);
    lowering.unconfined = unconfined_locals(members[body_index]);
    // Whatever an unsafe block names, it may write, and it may keep the address
    // and write through it later, from another unsafe block that never names it.
    // So every such name is treated as escaped, which is what makes each unsafe
    // block reach it, and as unconfined, since the writes there owed no
    // refinement (SPEC.md UNSAFE-003, TRUST.md TCB-UNSAFE-002).
    const std::vector<CXCursor> unsafe_blocks = unsafe_blocks_in(members[body_index], invariant_prefix);
    for (const CXCursor block : unsafe_blocks) {
        for (const unsigned named : named_declarations(block)) {
            lowering.escaped.insert(named);
            lowering.unconfined.insert(named);
        }
    }
    // Ghost state is decided for the whole body before any path is lowered: a
    // use by code that runs is an error wherever it stands, reached or not.
    if (!invariant_prefix.empty()) {
        GhostScan ghosts(invariant_prefix);
        ghosts.run(members[body_index]);
        function.ghost_calls = std::move(ghosts.calls);
        if (!ghosts.errors.empty()) {
            function.ghost_errors = std::move(ghosts.errors);
            function.body_rejection = "it declares or uses ghost state in a way the language does not allow";
            return;
        }
    }
    // A view cannot be returned: it would outlive every storage this body could
    // have formed it over, or designate caller storage no contract says stays
    // valid (SPEC.md STDMODEL-015, RFC 0020 §5).
    if (executable_state && function.result.representation.kind == source::RepresentationKind::Span) {
        function.body_rejection = "it returns a span, and a view is not returned: it could outlive the storage it "
                                  "views (SPEC.md STDMODEL-015)";
        return;
    }
    if (executable_state && source::is_sequence(function.result.representation.kind)) {
        lowering.library_models.insert(function.result.representation.kind);
    }
    Locals candidates;
    // A member function's implicit object is caller storage, one place per
    // leaf, rooted in the object's class: distinct members of it are distinct
    // storage, and any of them may be what a reference parameter designates
    // (SPEC.md CLASS-008, CLASS-010).
    if (executable_state && signature.receiver.has_value()) {
        for (const ReceiverLeaf& leaf : signature.receiver->leaves) {
            candidates.push_back(Local{.declaration = signature.receiver->record,
                                       .type = leaf.type,
                                       .external = true,
                                       .path = leaf.path,
                                       .spelling = leaf.spelling});
        }
    }
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (!executable_state) {
            continue;
        }
        const auto& parameter = function.parameters[signature.position(index)];
        // A vector or string parameter is the root of a modeled sequence: its
        // versions carry its length, and its element places are formed on
        // access. A span parameter is not tracked: it is a value the call
        // fixed, whose elements are caller storage reached under a capability
        // (RFC 0020 §3, §7).
        if (source::is_sequence(parameter.type.representation.kind)) {
            const std::string name = take(clang_getCursorSpelling(parameters[index]));
            if (!parameter.type.representation.rejection.empty()) {
                function.body_rejection = "parameter '" + name + "' has type '" + parameter.type.spelling +
                                          "', which is not modeled: " + parameter.type.representation.rejection;
                return;
            }
            auto element = sequence_element(parameters[index], clang_getCursorType(parameters[index]), &refinements);
            if (!element) {
                function.body_rejection = "parameter '" + name + "': " + element.error();
                return;
            }
            // No caller proof could establish that every element of a
            // container it passes satisfies a refinement, and an unverified
            // caller passes the same C++ type (SPEC.md STDMODEL-020).
            if (!element->refinements.empty()) {
                function.body_rejection = "parameter '" + name +
                                          "' is a container of refined elements, whose element validity no call can "
                                          "establish; a refined element type is modeled only for a local "
                                          "(SPEC.md STDMODEL-020)";
                return;
            }
            lowering.library_models.insert(parameter.type.representation.kind);
            if (parameter.type.representation.kind == source::RepresentationKind::Span) {
                if (parameter.passing != source::ParameterPassing::Value) {
                    function.body_rejection = "span parameter '" + name + "' is modeled only by value";
                    return;
                }
                continue;
            }
            Local root{.declaration = parameters[index],
                       .type = parameter.type,
                       .external = source::aliases_storage(parameter.passing),
                       .spelling = name};
            root.sequence = Local::Sequence{};
            root.sequence->kind = parameter.type.representation.kind;
            root.sequence->element = std::move(*element);
            root.sequence->external_elements = source::aliases_storage(parameter.passing);
            candidates.push_back(std::move(root));
            continue;
        }
        if (parameter.type.kind == TypeKind::Int || parameter.type.kind == TypeKind::Bool) {
            candidates.push_back(Local{.declaration = parameters[index],
                                       .type = parameter.type,
                                       .external = source::aliases_storage(parameter.passing),
                                       .spelling = take(clang_getCursorSpelling(parameters[index]))});
            continue;
        }
        // A by-value aggregate parameter is the callee's own copy of the
        // caller's value, so its members are ordinary storage of this body and
        // writing one has the same modeled effect as writing a local's member.
        // A parameter that aliases caller storage is not: another reference may
        // designate the same object, so what a write there reaches is not
        // decided here (RFC 0014 §4).
        //
        // A type whose members cannot all be enumerated is left untracked, and
        // a write to it is refused where it is written, as before.
        if (parameter.type.kind == TypeKind::Value && !source::aliases_storage(parameter.passing)) {
            std::vector<BodyLowering::AggregateLeaf> leaves;
            const std::string name = take(clang_getCursorSpelling(parameters[index]));
            if (!lowering.collect_type_leaves(parameter.type, name, {}, leaves)) {
                for (BodyLowering::AggregateLeaf& leaf : leaves) {
                    candidates.push_back(Local{.declaration = parameters[index],
                                               .type = leaf.type,
                                               .path = std::move(leaf.path),
                                               .spelling = std::move(leaf.spelling)});
                }
            }
            continue;
        }
        // An object a parameter designates by reference is caller storage
        // another reference may reach, so it is tracked as one whole place
        // whose version any write that may alias it replaces: a member read
        // after such a write projects a value nothing states, never the one
        // the parameter arrived with (SPEC.md 12.9, CLASS-010). A pointer is
        // left as it was: what it designates is a dereference place of its own.
        if (parameter.type.kind == TypeKind::Value && source::aliases_storage(parameter.passing) &&
            parameter.type.representation.kind != source::RepresentationKind::Pointer) {
            candidates.push_back(Local{.declaration = parameters[index],
                                       .type = parameter.type,
                                       .external = true,
                                       .spelling = take(clang_getCursorSpelling(parameters[index])),
                                       .read_only = true});
        }
    }
    // A parameter this body does not track has one value throughout it, which
    // an unsafe block could change without the change being seen. Such a block
    // is refused rather than followed by a stale value (SPEC.md UNSAFE-005).
    for (const CXCursor parameter : parameters) {
        const bool tracked = std::ranges::any_of(candidates, [&](const Local& candidate) {
            return clang_equalCursors(candidate.declaration, parameter) != 0;
        });
        if (tracked) {
            continue;
        }
        for (const CXCursor block : unsafe_blocks) {
            if (may_rebind(block, parameter)) {
                const source::SourceLocation at = presumed_location(clang_getCursorLocation(block));
                function.body_rejection = "the unsafe block at " + at.file + ":" + std::to_string(at.line) +
                                          " may change parameter '" + take(clang_getCursorSpelling(parameter)) +
                                          "' itself, which this body does not track, so what it holds afterwards "
                                          "could not be followed";
                return;
            }
        }
    }
    // Every tracked parameter is followed where an unsafe block may reach it.
    std::vector<bool> needed(candidates.size(), lowering.has_post_state() || !unsafe_blocks.empty());
    // A container parameter is always followed: its length is read through its
    // root wherever the body names it.
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (candidates[index].sequence.has_value()) {
            needed[index] = true;
        }
    }
    mark_writes(members[body_index], candidates, needed);
    WriteScan aliases{&candidates, &needed};
    clang_visitChildren(
        members[body_index],
        [](CXCursor child, CXCursor, CXClientData data) {
            auto& scan = *static_cast<WriteScan*>(data);
            if (clang_getCursorKind(child) == CXCursor_VarDecl &&
                source::aliases_storage(passing_of(clang_getCursorType(child)))) {
                if (auto target = written_storage(child, *scan.locals))
                    (*scan.written)[*target] = true;
            }
            return CXChildVisit_Recurse;
        },
        &aliases);
    Locals entry;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!needed[index])
            continue;
        candidates[index].version = lowering.next_version++;
        entry.push_back(candidates[index]);
    }
    function.returned_value = lowering.lower_statements(Continuation{nullptr, &statements, 0}, entry, 0);
    if (function.returned_value) {
        for (std::size_t index = entry.size(); index > 0; --index) {
            const Local& local = entry[index - 1];
            // A leaf of the implicit object enters as the parameter it is.
            if (signature.receiver.has_value() &&
                clang_equalCursors(local.declaration, signature.receiver->record) != 0) {
                const std::optional<std::size_t> leaf = signature.receiver->leaf_at(local.path);
                if (!leaf.has_value()) {
                    function.returned_value.reset();
                    lowering.rejection = "the implicit object's '" + local.spelling + "' is not one of its leaves";
                    break;
                }
                Expr value;
                value.type = function.parameters[*leaf].type;
                value.location = function.location;
                value.node = ParameterRef{static_cast<std::uint32_t>(*leaf), local.spelling};
                *function.returned_value = lowering.bind(local.version, place_of(entry, index - 1), std::move(value),
                                                         std::move(*function.returned_value), cursor);
                continue;
            }
            const std::optional<std::uint32_t> position = signature.position_of(local.declaration);
            if (!position.has_value()) {
                function.returned_value.reset();
                lowering.rejection = "'" + local.spelling + "' is not a parameter of this body";
                break;
            }
            Expr value;
            value.type = function.parameters[*position].type;
            value.location = function.location;
            value.node = ParameterRef{*position, take(clang_getCursorSpelling(local.declaration))};
            // A member entry holds a projection of the parameter's value rather
            // than the whole of it: the parameter arrives as one value, and its
            // members are the places inside that value.
            bool projected = true;
            for (const PlaceStep& step : local.path) {
                if (step.index >= value.type.projections.size()) {
                    projected = false;
                    break;
                }
                Expr component;
                component.type = value.type.projections[step.index];
                component.location = value.location;
                component.node = Projection{step.index, {std::move(value)}};
                value = std::move(component);
            }
            if (!projected) {
                function.returned_value.reset();
                lowering.rejection = "parameter '" + take(clang_getCursorSpelling(local.declaration)) +
                                     "' has a member this implementation cannot state as a projection of it";
                break;
            }
            *function.returned_value = lowering.bind(local.version, place_of(entry, index - 1), std::move(value),
                                                     std::move(*function.returned_value), cursor);
        }
    }
    if (!function.returned_value) {
        function.body_rejection = lowering.rejection.empty() ? "every path must return a value" : lowering.rejection;
    }
    function.loop_invariants = std::move(lowering.consumed_invariants);
    function.path_contradictions = std::move(lowering.consumed_contradictions);
    function.path_splits = std::move(lowering.consumed_splits);
    function.unsafe_regions = std::move(lowering.consumed_unsafe);
    function.library_models.assign(lowering.library_models.begin(), lowering.library_models.end());
}

std::size_t physical_offset(CXCursor cursor) {
    unsigned offset = 0;
    clang_getFileLocation(clang_getCursorLocation(cursor), nullptr, nullptr, nullptr, &offset);
    return static_cast<std::size_t>(offset);
}

// The template arguments a specialization was instantiated at, as Clang
// resolved them (SPEC.md 42).
//
// Clang owns substitution: these are read back only to pair a specialization
// with the instantiation of its own contract, never to perform substitution
// here. A form this implementation does not read becomes `Other`, which
// compares equal only to the same position of another argument list and so
// never merges two specializations that differ in it.
std::vector<TemplateArgument> template_arguments_of(CXCursor cursor) {
    std::vector<TemplateArgument> arguments;
    const int count = clang_Cursor_getNumTemplateArguments(cursor);
    for (int index = 0; index < count; ++index) {
        const auto position = static_cast<unsigned>(index);
        TemplateArgument argument;
        switch (clang_Cursor_getTemplateArgumentKind(cursor, position)) {
            case CXTemplateArgumentKind_Integral:
                argument.kind = TemplateArgument::Kind::Integral;
                argument.integral = clang_Cursor_getTemplateArgumentValue(cursor, position);
                break;
            case CXTemplateArgumentKind_Type: {
                argument.kind = TemplateArgument::Kind::Type;
                const CXType type = clang_Cursor_getTemplateArgumentType(cursor, position);
                argument.spelling = take(clang_getTypeSpelling(clang_getCanonicalType(type)));
                break;
            }
            default:
                argument.kind = TemplateArgument::Kind::Other;
                argument.spelling = std::to_string(index);
                break;
        }
        arguments.push_back(std::move(argument));
    }
    return arguments;
}

// Whether `cursor` is a specialization of a function template, and if so the
// primary template it came from.
std::optional<CXCursor> specialized_template(CXCursor cursor) {
    if (clang_Cursor_getNumTemplateArguments(cursor) <= 0) {
        return std::nullopt;
    }
    const CXCursor primary = clang_getSpecializedCursorTemplate(cursor);
    if (clang_Cursor_isNull(primary) != 0) {
        return std::nullopt;
    }
    return primary;
}

struct Collector {
    const Selection* selection = nullptr;
    std::vector<CXCursor> selected;
    // Specializations of verified function templates this unit instantiated,
    // deduplicated by USR.
    std::vector<CXCursor> specializations;
    std::vector<CXCursor> functions;
    std::vector<CXCursor> unverified_storage;
};

bool is_selected(CXCursor cursor, const Selection& selection) {
    const std::string name = take(clang_getCursorSpelling(cursor));
    if (!selection.specification_prefix.empty() && name.starts_with(selection.specification_prefix)) {
        return true;
    }

    const auto offset = physical_offset(cursor);
    return std::ranges::find(selection.offsets, offset) != selection.offsets.end();
}

CXChildVisitResult collect(CXCursor cursor, CXCursor, CXClientData data) {
    auto& collector = *static_cast<Collector*>(data);
    const CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind == CXCursor_Namespace || kind == CXCursor_UnexposedDecl || kind == CXCursor_LinkageSpec ||
        kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl || kind == CXCursor_ClassTemplate ||
        kind == CXCursor_UnionDecl) {
        return CXChildVisit_Recurse;
    }

    // A member function is selected like any other: a verified member by the
    // offset of its declaration in the class, and a contract probe of one --
    // itself a member of that class -- by its generated name.
    if ((kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod) && is_selected(cursor, *collector.selection)) {
        collector.selected.push_back(cursor);
    }
    if (kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod || kind == CXCursor_FunctionTemplate ||
        kind == CXCursor_Constructor || kind == CXCursor_Destructor) {
        collector.functions.push_back(cursor);
        const auto name = take(clang_getCursorSpelling(cursor));
        const bool generated = !collector.selection->specification_prefix.empty() &&
                               name.starts_with(collector.selection->specification_prefix);
        const bool verified = std::ranges::find(collector.selection->verified_offsets, physical_offset(cursor)) !=
                              collector.selection->verified_offsets.end();
        return generated || verified ? CXChildVisit_Continue : CXChildVisit_Recurse;
    }
    // A variable declared outside a verified body is storage ordinary C++
    // establishes without any obligation, so a refinement on it would be a fact
    // nothing proved.
    //
    // A data member is different: it has no value of its own until an object is
    // constructed, and every construction and write is checked where it happens
    // (SPEC.md 17.6). Declaring one is therefore sound, and the objects built
    // from it are what carry the obligations.
    if (kind == CXCursor_VarDecl)
        collector.unverified_storage.push_back(cursor);

    return collector.selection->refinements.empty() ? CXChildVisit_Continue : CXChildVisit_Recurse;
}

// Collect the specializations of function templates that this unit actually
// instantiated (SPEC.md 42, TEMPLATE-001).
//
// An implicit instantiation is not a child of the translation unit cursor, so
// it cannot be found by walking declarations: it is reached from the use that
// caused it. Every reference is followed and the referenced declaration taken,
// which is the specialization Clang selected and instantiated. Nothing here
// decides which specialization a use denotes -- Clang already did, including
// overload resolution and constraints (SPEC.md TEMPLATE-002).
CXChildVisitResult collect_specializations(CXCursor cursor, CXCursor, CXClientData data) {
    auto& collector = *static_cast<Collector*>(data);
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind != CXCursor_CallExpr && kind != CXCursor_DeclRefExpr && kind != CXCursor_MemberRefExpr) {
        return CXChildVisit_Recurse;
    }
    const CXCursor referenced = clang_getCursorReferenced(cursor);
    if (clang_Cursor_isNull(referenced) != 0) {
        return CXChildVisit_Recurse;
    }
    const auto primary = specialized_template(referenced);
    if (!primary) {
        return CXChildVisit_Recurse;
    }
    // Only a specialization of a declaration this unit marked verified is
    // checked here, or of a probe the projector generated for one. The
    // primary's own location is what the projector recorded, because that is
    // where the author wrote the declaration.
    const auto offset = physical_offset(*primary);
    const auto name = take(clang_getCursorSpelling(referenced));
    const bool generated = !collector.selection->specification_prefix.empty() &&
                           name.starts_with(collector.selection->specification_prefix);
    if (!generated && std::ranges::find(collector.selection->offsets, offset) == collector.selection->offsets.end()) {
        return CXChildVisit_Recurse;
    }
    const auto usr = take(clang_getCursorUSR(referenced));
    const bool known = std::ranges::any_of(
        collector.specializations, [&](CXCursor candidate) { return take(clang_getCursorUSR(candidate)) == usr; });
    if (!known) {
        collector.specializations.push_back(referenced);
        // The instantiated body refers to this specialization's own contract
        // probes, which C++ instantiates at the same arguments. Those
        // instantiations exist only inside the body, so they are collected by
        // descending into it: that is what makes the proposition checked the
        // one written for these arguments (SPEC.md TEMPLATE-001).
        clang_visitChildren(referenced, collect_specializations, &collector);
    }
    return CXChildVisit_Recurse;
}

// Detect an explicit refinement use at an unverified storage/callable boundary.
// These are Clang declaration-reference edges, including ordinary aliases and
// type constructors; no pointer/pointee or container-wide fact is inferred.
std::optional<std::string> refinement_use(CXCursor declaration, const Selection& selection, unsigned depth = 0,
                                          std::unordered_set<std::size_t>* visited = nullptr) {
    if (depth > kMaxExpressionDepth)
        return "unresolved alias chain";

    // Record types reach one another, and themselves: a glibc `FILE` is a
    // `struct _IO_FILE` whose fields point back at `_IO_FILE`. Walking that
    // without remembering where we have been revisits the same declarations
    // until the depth guard trips, and the guard's "unresolved alias chain"
    // would then be reported as a refinement on a standard header that
    // declares none. A declaration is therefore visited once per query.
    std::unordered_set<std::size_t> owned;

    if (visited == nullptr)
        visited = &owned;

    if (!visited->insert(physical_offset(declaration)).second)
        return std::nullopt;
    const auto entry =
        std::ranges::find(selection.refinements, physical_offset(declaration), &Selection::Refinement::alias_offset);
    if (entry != selection.refinements.end())
        return entry->name;
    const auto initializer = clang_Cursor_getVarDeclInitializer(declaration);
    for (const auto child : children_of(declaration)) {
        const auto kind = clang_getCursorKind(child);
        if ((!clang_Cursor_isNull(initializer) && clang_equalCursors(initializer, child)) ||
            kind == CXCursor_ParmDecl || clang_isStatement(kind))
            break;
        if (kind == CXCursor_TypeRef || kind == CXCursor_TemplateRef) {
            const auto referenced = clang_getCursorReferenced(child);
            const auto referenced_kind = clang_getCursorKind(referenced);
            if (referenced_kind == CXCursor_TypeAliasDecl || referenced_kind == CXCursor_TypedefDecl ||
                referenced_kind == CXCursor_TypeAliasTemplateDecl) {
                if (auto use = refinement_use(referenced, selection, depth + 1, visited))
                    return use;
            }
        }
        if (kind == CXCursor_TypeAliasDecl) {
            if (auto use = refinement_use(child, selection, depth + 1, visited))
                return use;
        }
        // A record's refined member is storage this declaration establishes
        // too. Constructing the object outside a verified body would put a
        // value in that member without proving its predicate, so the record
        // counts as a refinement use exactly as a directly refined type does
        // (SPEC.md 17.6).
        if (kind == CXCursor_TypeRef) {
            const auto definition = clang_getCursorDefinition(clang_getCursorReferenced(child));
            const auto definition_kind = clang_getCursorKind(definition);
            if (definition_kind == CXCursor_StructDecl || definition_kind == CXCursor_ClassDecl) {
                for (const auto field : children_of(definition)) {
                    if (clang_getCursorKind(field) != CXCursor_FieldDecl)
                        continue;
                    if (auto use = refinement_use(field, selection, depth + 1, visited))
                        return use;
                }
            }
        }
    }
    return std::nullopt;
}

Severity convert_severity(CXDiagnosticSeverity severity) {
    switch (severity) {
        case CXDiagnostic_Ignored:
        case CXDiagnostic_Note:
            return Severity::Note;
        case CXDiagnostic_Warning:
            return Severity::Warning;
        case CXDiagnostic_Error:
            return Severity::Error;
        case CXDiagnostic_Fatal:
            return Severity::Fatal;
    }
    return Severity::Error;
}

// The schema describes only syntax emitted by the projector. Every C++ leaf,
// parameter type and declaration reference is resolved independently by Clang.
std::expected<Expr, std::string> build_formal(CXCursor cursor, const source::ProjectionShape& shape,
                                              const Signature& signature, unsigned depth) {
    using Kind = source::ProjectionKind;
    if (depth > kMaxExpressionDepth)
        return std::unexpected("formal proposition nests too deeply");
    // A capability is a statement about storage, not a value, so it cannot be an
    // operand of a logical connective that the kernel would then have to check.
    // Combining capabilities is a contract-level matter: state them as separate
    // clauses (SPEC.md 12.10).
    if (shape.kind == Kind::Readable || shape.kind == Kind::Writable || shape.kind == Kind::Capabilities)
        return std::unexpected("a memory capability states storage permission, not a value, so it cannot be an "
                               "operand of a proposition");
    if (shape.kind == Kind::Expression) {
        if (!shape.children.empty())
            return std::unexpected("malformed expression projection");
        return build_expression(cursor, signature, {}, 0);
    }
    while (clang_getCursorKind(cursor) == CXCursor_UnexposedExpr || clang_getCursorKind(cursor) == CXCursor_ParenExpr) {
        const auto children = children_of(cursor);
        if (children.size() != 1)
            return std::unexpected("malformed formal expression wrapper");
        cursor = children[0];
    }
    Expr result;
    result.type.kind = TypeKind::Proposition;
    result.type.spelling = "Prop";
    result.location = presumed_location(clang_getCursorLocation(cursor));

    if (shape.kind == Kind::Equality) {
        if (!shape.children.empty() || clang_getCursorKind(cursor) != CXCursor_CallExpr)
            return std::unexpected("malformed equality probe");
        const auto method = clang_getCursorReferenced(cursor);
        const auto formals = parameters_of(method);
        if (clang_getCursorKind(method) != CXCursor_CXXMethod || formals.size() != 2 ||
            clang_Cursor_getNumArguments(cursor) != 3)
            return std::unexpected("malformed equality operands");
        const auto first = clang_getCanonicalType(clang_getCursorType(formals[0]));
        const auto second = clang_getCanonicalType(clang_getCursorType(formals[1]));
        if (clang_equalTypes(first, second) == 0)
            return std::unexpected("equality operand types differ");
        // The equality helper takes its operands by reference so it imposes no
        // copy on the values compared. The operand type is the referent's.
        FormalEquality equality{convert_type(first, 0, ReferenceModel::Referent), {}};
        // The first operator() argument is the closure object.
        for (unsigned index = 1; index < 3; ++index)
            equality.operands.push_back(build_expression(clang_Cursor_getArgument(cursor, index), signature, {}, 0));
        result.node = std::move(equality);
        return result;
    }

    if (clang_getCursorKind(cursor) != CXCursor_LambdaExpr)
        return std::unexpected("formal scope is not a projected C++ lambda");
    std::vector<CXCursor> binders;
    std::vector<CXCursor> bodies;
    for (const auto child : children_of(cursor)) {
        if (clang_getCursorKind(child) == CXCursor_ParmDecl)
            binders.push_back(child);
        if (clang_getCursorKind(child) == CXCursor_CompoundStmt)
            bodies.push_back(child);
    }
    if (bodies.size() != 1)
        return std::unexpected("formal scope requires one body");
    const auto statements = children_of(bodies[0]);
    if (shape.kind == Kind::Universal) {
        if (binders.empty() || shape.children.size() != 1 || statements.size() != 1 ||
            clang_getCursorKind(statements[0]) != CXCursor_ReturnStmt)
            return std::unexpected("forall requires binders and one proposition");
        const auto values = children_of(statements[0]);
        if (values.size() != 1)
            return std::unexpected("forall has no proposition");
        Signature scope = signature;
        scope.parameters.insert(scope.parameters.end(), binders.begin(), binders.end());
        auto body = build_formal(values[0], shape.children[0], scope, depth + 1);
        if (!body)
            return body;
        Universal quantified;
        for (const auto binder : binders)
            quantified.binders.push_back(convert_type(clang_getCursorType(binder)));
        quantified.body.push_back(std::move(*body));
        result.node = std::move(quantified);
        return result;
    }
    if (shape.kind == Kind::Implication || shape.kind == Kind::Conjunction || shape.kind == Kind::Disjunction ||
        shape.kind == Kind::Equivalence) {
        if (!binders.empty() || shape.children.size() != 2 || statements.size() != 2)
            return std::unexpected("logical connective requires exactly two propositions");
        std::vector<Expr> operands;
        for (std::size_t index = 0; index < 2; ++index) {
            auto operand = build_formal(statements[index], shape.children[index], signature, depth + 1);
            if (!operand)
                return operand;
            operands.push_back(std::move(*operand));
        }
        if (shape.kind == Kind::Implication) {
            result.node = Implication{std::move(operands)};
        } else {
            const auto kind = shape.kind == Kind::Conjunction   ? Connective::Kind::Conjunction
                              : shape.kind == Kind::Disjunction ? Connective::Kind::Disjunction
                                                                : Connective::Kind::Equivalence;
            result.node = Connective{kind, std::move(operands)};
        }
        return result;
    }
    return std::unexpected("unknown formal projection form");
}

// A capability probe's body is the projected `([](auto&&...) {})(operands)`:
// a lambda that is declared, called for its operand types and does nothing.
// Decoding it yields the capability's operands, resolved by Clang, and never an
// `Expr` that could reach the kernel.
std::expected<Capability, std::string> build_capability(CXCursor cursor, source::ProjectionKind kind,
                                                        const Signature& signature) {
    const std::vector<CXCursor>& parameters = signature.parameters;
    while (clang_getCursorKind(cursor) == CXCursor_UnexposedExpr || clang_getCursorKind(cursor) == CXCursor_ParenExpr) {
        const auto children = children_of(cursor);
        if (children.size() != 1)
            return std::unexpected("malformed memory capability wrapper");
        cursor = children[0];
    }
    if (clang_getCursorKind(cursor) != CXCursor_CallExpr)
        return std::unexpected("malformed memory capability probe");
    // The first argument of the projected call is the closure object; the
    // capability's own operands follow it.
    const int arguments = clang_Cursor_getNumArguments(cursor);
    if (arguments != 2 && arguments != 3)
        return std::unexpected("a memory capability states a pointer and an optional element count");
    Capability capability;
    capability.kind =
        kind == source::ProjectionKind::Readable ? Capability::Kind::Readable : Capability::Kind::Writable;
    capability.location = presumed_location(clang_getCursorLocation(cursor));

    // In a contract the capability's pointer is one of the function's
    // parameters, so the place it names is that parameter's storage. Resolving
    // it here keeps Clang the authority on which declaration the spelling
    // refers to.
    CXCursor pointer = clang_Cursor_getArgument(cursor, 1);
    while (clang_getCursorKind(pointer) == CXCursor_UnexposedExpr ||
           clang_getCursorKind(pointer) == CXCursor_ParenExpr) {
        const auto nested = children_of(pointer);
        if (nested.size() != 1)
            break;
        pointer = nested[0];
    }
    if (clang_getCursorKind(pointer) != CXCursor_DeclRefExpr)
        return std::unexpected("a memory capability names a pointer parameter");
    const CXCursor declaration = clang_getCursorReferenced(pointer);
    const auto at = std::ranges::find_if(
        parameters, [&](CXCursor candidate) { return clang_equalCursors(candidate, declaration) != 0; });
    if (at == parameters.end())
        return std::unexpected("a memory capability names a pointer parameter of this function");
    // A span parameter states its own extent, so its capability covers every
    // element it views and takes no count (SPEC.md STDMODEL-016).
    const Type named = convert_type(clang_getCursorType(declaration));
    const bool span = named.representation.kind == source::RepresentationKind::Span;
    if (span && !named.representation.rejection.empty())
        return std::unexpected("'" + named.spelling + "' is not modeled: " + named.representation.rejection);
    if (span && arguments != 2)
        return std::unexpected("a span states its own extent, so a capability of a span takes no element count");
    if (!span && clang_getCanonicalType(clang_getCursorType(declaration)).kind != CXType_Pointer)
        return std::unexpected("a memory capability names a pointer or a span");
    capability.pointer.root.kind = PlaceRoot::Kind::Parameter;
    // The callable position, past a member function's implicit object.
    capability.pointer.root.id = signature.position(static_cast<std::size_t>(at - parameters.begin()));
    capability.pointer.spelling = take(clang_getCursorSpelling(declaration));

    if (arguments == 3)
        capability.extent.push_back(build_expression(clang_Cursor_getArgument(cursor, 2), signature, {}, 0));
    return capability;
}

// A capability clause is either one capability or a conjunction of them, which
// the projector emitted as a lambda holding one statement per operand.
std::expected<std::vector<Capability>, std::string> build_capabilities(CXCursor cursor,
                                                                       const source::ProjectionShape& shape,
                                                                       const Signature& signature, unsigned depth) {
    if (depth > kMaxExpressionDepth)
        return std::unexpected("memory capabilities nest too deeply");
    if (shape.kind != source::ProjectionKind::Capabilities) {
        auto one = build_capability(cursor, shape.kind, signature);
        if (!one)
            return std::unexpected(one.error());
        return std::vector<Capability>{std::move(*one)};
    }
    while (clang_getCursorKind(cursor) == CXCursor_UnexposedExpr || clang_getCursorKind(cursor) == CXCursor_ParenExpr) {
        const auto nested = children_of(cursor);
        if (nested.size() != 1)
            return std::unexpected("malformed memory capability wrapper");
        cursor = nested[0];
    }
    if (clang_getCursorKind(cursor) != CXCursor_LambdaExpr || shape.children.size() != 2)
        return std::unexpected("malformed conjunction of memory capabilities");
    std::vector<CXCursor> bodies;
    for (const auto child : children_of(cursor)) {
        if (clang_getCursorKind(child) == CXCursor_CompoundStmt)
            bodies.push_back(child);
    }
    if (bodies.size() != 1)
        return std::unexpected("a conjunction of memory capabilities requires one body");
    const auto statements = children_of(bodies[0]);
    if (statements.size() != 2)
        return std::unexpected("a conjunction of memory capabilities requires two operands");
    std::vector<Capability> capabilities;
    for (std::size_t index = 0; index < 2; ++index) {
        auto operand = build_capabilities(statements[index], shape.children[index], signature, depth + 1);
        if (!operand)
            return operand;
        capabilities.insert(capabilities.end(), std::make_move_iterator(operand->begin()),
                            std::make_move_iterator(operand->end()));
    }
    return capabilities;
}

bool is_capability_shape(const source::ProjectionShape& shape) {
    return shape.kind == source::ProjectionKind::Readable || shape.kind == source::ProjectionKind::Writable ||
           shape.kind == source::ProjectionKind::Capabilities;
}

// Whether a conjunction joins a memory capability with an ordinary predicate
// somewhere among its conjuncts.
bool mixes_capabilities(const source::ProjectionShape& shape, unsigned depth = 0) {
    if (depth > kMaxExpressionDepth || shape.kind != source::ProjectionKind::Conjunction) {
        return false;
    }
    return std::ranges::any_of(shape.children, [&](const source::ProjectionShape& child) {
        return is_capability_shape(child) || mixes_capabilities(child, depth + 1);
    });
}

// A conjunction of memory capabilities and ordinary predicates, read apart
// (SPEC.md STDMODEL-016, ARCHITECTURE.md 25): each capability leaves on the
// capability channel, and the predicates, conjoined in the order written, are
// the proposition the clause states to the kernel. The two channels together
// mean exactly the conjunction: a caller owes every part, and the body supposes
// every part.
std::expected<void, std::string> split_conjunction(CXCursor cursor, const source::ProjectionShape& shape,
                                                   const Signature& signature, std::vector<Capability>& capabilities,
                                                   std::vector<Expr>& propositions, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return std::unexpected("formal proposition nests too deeply");
    }
    if (is_capability_shape(shape)) {
        auto found = build_capabilities(cursor, shape, signature, depth + 1);
        if (!found) {
            return std::unexpected(found.error());
        }
        capabilities.insert(capabilities.end(), std::make_move_iterator(found->begin()),
                            std::make_move_iterator(found->end()));
        return {};
    }
    if (!mixes_capabilities(shape)) {
        auto proposition = build_formal(cursor, shape, signature, depth + 1);
        if (!proposition) {
            return std::unexpected(proposition.error());
        }
        propositions.push_back(std::move(*proposition));
        return {};
    }
    cursor = strip_parens(cursor);
    if (clang_getCursorKind(cursor) != CXCursor_LambdaExpr || shape.children.size() != 2) {
        return std::unexpected("malformed conjunction of a memory capability and a predicate");
    }
    std::vector<CXCursor> bodies;
    for (const auto child : children_of(cursor)) {
        if (clang_getCursorKind(child) == CXCursor_CompoundStmt) {
            bodies.push_back(child);
        }
    }
    if (bodies.size() != 1 || children_of(bodies.front()).size() != 2) {
        return std::unexpected("malformed conjunction of a memory capability and a predicate");
    }
    const std::vector<CXCursor> operands = children_of(bodies.front());
    for (std::size_t index = 0; index < 2; ++index) {
        if (auto split = split_conjunction(operands[index], shape.children[index], signature, capabilities,
                                           propositions, depth + 1);
            !split) {
            return split;
        }
    }
    return {};
}

void extract_formal(Function& function, CXCursor cursor, const Signature& signature,
                    const source::ProjectionShape& shape) {
    function.has_body = true;
    function.body_rejection = "malformed formal proposition probe";
    const bool is_capability = shape.kind == source::ProjectionKind::Readable ||
                               shape.kind == source::ProjectionKind::Writable ||
                               shape.kind == source::ProjectionKind::Capabilities;
    const bool mixed = mixes_capabilities(shape);
    for (const auto child : children_of(cursor)) {
        if (clang_getCursorKind(child) != CXCursor_CompoundStmt)
            continue;
        const auto statements = children_of(child);
        if (statements.size() != 1)
            return;
        // A capability probe states no value, so its body is the projected call
        // as a statement rather than a return.
        if (is_capability) {
            auto capabilities = build_capabilities(statements[0], shape, signature, 0);
            if (!capabilities) {
                function.body_rejection = capabilities.error();
                return;
            }
            function.capabilities = std::move(*capabilities);
            function.body_rejection.reset();
            return;
        }
        if (clang_getCursorKind(statements[0]) != CXCursor_ReturnStmt)
            return;
        const auto values = children_of(statements[0]);
        if (values.size() != 1)
            return;
        if (mixed) {
            std::vector<Capability> capabilities;
            std::vector<Expr> propositions;
            if (auto split = split_conjunction(values[0], shape, signature, capabilities, propositions, 0); !split) {
                function.body_rejection = split.error();
                return;
            }
            if (propositions.empty() || capabilities.empty()) {
                return;
            }
            Expr conjoined = std::move(propositions.front());
            for (std::size_t index = 1; index < propositions.size(); ++index) {
                Expr next;
                next.type.kind = TypeKind::Proposition;
                next.type.spelling = "Prop";
                next.location = conjoined.location;
                next.node =
                    Connective{Connective::Kind::Conjunction, {std::move(conjoined), std::move(propositions[index])}};
                conjoined = std::move(next);
            }
            function.capabilities = std::move(capabilities);
            function.returned_value = std::move(conjoined);
            function.body_rejection.reset();
            return;
        }
        auto expression = build_formal(values[0], shape, signature, 0);
        if (!expression) {
            function.body_rejection = expression.error();
            return;
        }
        function.returned_value = std::move(*expression);
        function.body_rejection.reset();
        return;
    }
}

// A member function's standing as a verified callable: its implicit object, or
// why this implementation does not verify it (SPEC.md CLASS-008, CLASS-014,
// CLASS-015). A function that is not a member, and a static member, has no
// implicit object and nothing to refuse here.
struct MemberStanding {
    std::optional<Receiver> receiver;
    std::optional<std::string> rejection;
};

MemberStanding member_standing(CXCursor cursor, const std::vector<Selection::Refinement>& known) {
    MemberStanding standing;
    if (clang_getCursorKind(cursor) != CXCursor_CXXMethod || clang_CXXMethod_isStatic(cursor) != 0) {
        return standing;
    }
    // Checked on the declaration rather than read off its spelling: a function
    // overriding a virtual one is virtual whether or not it says so.
    if (clang_CXXMethod_isVirtual(cursor) != 0) {
        standing.rejection = "it is virtual: a call through its base interface runs whichever override the dynamic "
                             "type selects, and override substitutability is not checked by this implementation "
                             "(SPEC.md CONTRACT-014, CLASS-006, CLASS-014)";
        return standing;
    }
    const CXCursorKind parent = clang_getCursorKind(clang_getCursorSemanticParent(cursor));
    if (parent == CXCursor_ClassTemplate || parent == CXCursor_ClassTemplatePartialSpecialization) {
        standing.rejection = "it is a member of a class template, whose contract would have to be checked at every "
                             "specialization of the class, which nothing forces here (SPEC.md CLASS-015)";
        return standing;
    }
    const CXCursorKind lexical = clang_getCursorKind(clang_getCursorLexicalParent(cursor));
    if (lexical != CXCursor_StructDecl && lexical != CXCursor_ClassDecl && lexical != CXCursor_UnionDecl) {
        standing.rejection = "its contract is stated on a definition outside its class; a member function's contract "
                             "is stated on its declaration in the class, and the definition inherits it (SPEC.md "
                             "CONTRACT-005, CLASS-008)";
        return standing;
    }
    auto receiver = receiver_of(cursor, &known);
    if (!receiver) {
        standing.rejection =
            "its implicit object is not one this implementation models: " + receiver.error() + " (SPEC.md CLASS-015)";
        return standing;
    }
    standing.receiver = std::move(*receiver);
    return standing;
}

} // namespace

const Function* TranslationUnit::find_by_usr(std::string_view usr) const {
    for (const Function& function : functions) {
        if (function.usr == usr) {
            return &function;
        }
    }
    return nullptr;
}

const Function* TranslationUnit::find_by_name(std::string_view name) const {
    for (const Function& function : functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return nullptr;
}

// The one function declared at `offset`, or nothing when two different
// functions share it: an ambiguous declaration is not resolved by guessing.
//
// Two records of the *same* function are not two functions. An explicit
// specialization is reached both as a declaration and as its definition, and
// both carry one USR, so identity decides this rather than record count.
const Function* TranslationUnit::find_at_offset(std::size_t offset) const {
    const Function* found = nullptr;
    for (const Function& function : functions) {
        if (function.analysis_offset == offset) {
            if (found != nullptr && found->usr != function.usr)
                return nullptr;
            // Prefer the record carrying a body: the contract is discharged
            // from the definition.
            if (found == nullptr || (function.has_body && !found->has_body))
                found = &function;
        }
    }
    return found;
}

std::vector<const Function*> TranslationUnit::find_specializations_at_offset(std::size_t offset) const {
    std::vector<const Function*> found;
    for (const Function& function : functions) {
        if (function.analysis_offset == offset && !function.primary_usr.empty()) {
            found.push_back(&function);
        }
    }
    return found;
}

std::expected<TranslationUnit, std::string> parse(const ParseRequest& request) {
    CXIndex index = clang_createIndex(/*excludeDeclarationsFromPCH=*/0, /*displayDiagnostics=*/0);
    if (index == nullptr) {
        return std::unexpected("could not create a Clang index");
    }

    struct ReleaseIndex {
        CXIndex index;
        ~ReleaseIndex() {
            clang_disposeIndex(index);
        }
    } release_index{index};

    std::vector<const char*> argv;
    argv.reserve(request.arguments.size());
    for (const std::string& argument : request.arguments) {
        argv.push_back(argument.c_str());
    }

    CXTranslationUnit unit = nullptr;
    CXUnsavedFile unsaved{};
    if (request.content) {
        unsaved.Filename = request.path.c_str();
        unsaved.Contents = request.content->data();
        unsaved.Length = static_cast<unsigned long>(request.content->size());
    }
    const CXErrorCode error = clang_parseTranslationUnit2(
        index, request.path.c_str(), argv.data(), static_cast<int>(argv.size()), request.content ? &unsaved : nullptr,
        request.content ? 1u : 0u, CXTranslationUnit_None, &unit);
    if (error != CXError_Success || unit == nullptr) {
        return std::unexpected("Clang failed to parse '" + request.path + "'");
    }

    struct ReleaseUnit {
        CXTranslationUnit unit;
        ~ReleaseUnit() {
            clang_disposeTranslationUnit(unit);
        }
    } release_unit{unit};

    TranslationUnit result;

    const unsigned diagnostic_count = clang_getNumDiagnostics(unit);
    for (unsigned index_of_diagnostic = 0; index_of_diagnostic < diagnostic_count; ++index_of_diagnostic) {
        CXDiagnostic diagnostic = clang_getDiagnostic(unit, index_of_diagnostic);
        Diagnostic converted;
        converted.severity = convert_severity(clang_getDiagnosticSeverity(diagnostic));
        converted.message = take(clang_getDiagnosticSpelling(diagnostic));
        converted.location = presumed_location(clang_getDiagnosticLocation(diagnostic));
        clang_disposeDiagnostic(diagnostic);

        if (converted.severity == Severity::Error || converted.severity == Severity::Fatal) {
            result.has_errors = true;
        }
        result.diagnostics.push_back(std::move(converted));
    }

    // A rejected unit is never verified, and libclang's layout queries can
    // crash on the error types of its recovery expressions.
    if (result.has_errors && !request.recover_bindings && !request.recover_contract_types)
        return result;

    Collector collector;
    collector.selection = &request.selection;
    clang_visitChildren(clang_getTranslationUnitCursor(unit), collect, &collector);
    // A second pass for template specializations, which are reached from their
    // uses rather than from the declaration list (SPEC.md 42).
    clang_visitChildren(clang_getTranslationUnitCursor(unit), collect_specializations, &collector);
    for (const CXCursor& specialization : collector.specializations) {
        collector.selected.push_back(specialization);
    }
    if (result.has_errors && request.recover_contract_types && !request.recover_bindings) {
        // Recover only canonical void return identities, never bodies, layout,
        // obligations or facts from an erroneous AST. The corrected projection
        // must pass a fresh Clang analysis before verification can proceed.
        for (const auto cursor : collector.selected) {
            const auto offset = physical_offset(cursor);
            if (std::ranges::find(request.selection.verified_offsets, offset) ==
                    request.selection.verified_offsets.end() ||
                clang_getCanonicalType(clang_getCursorResultType(cursor)).kind != CXType_Void)
                continue;
            Function function;
            function.analysis_offset = offset;
            function.result.kind = TypeKind::Void;
            result.functions.push_back(std::move(function));
        }
        return result;
    }

    // An erased return alias is not evidence. Check even ordinary declarations
    // that were not selected for body elaboration. A verified redeclaration may
    // establish the same callable only through Clang's declaration identity.
    for (const auto cursor : collector.functions) {
        if (request.selection.refinements.empty())
            break;
        const auto name = take(clang_getCursorSpelling(cursor));
        if (!request.selection.specification_prefix.empty() && name.starts_with(request.selection.specification_prefix))
            continue;
        const auto refined = refinement_use(cursor, request.selection);
        if (!refined)
            continue;
        const auto canonical = clang_getCanonicalCursor(cursor);
        // A template's specializations are the functions that get verified, and
        // each reports the primary's location rather than its own. The
        // declaration the author marked `verified` is therefore the primary, so
        // a specialization is matched through it (SPEC.md TEMPLATE-001).
        const auto declared_offset = [](CXCursor candidate) {
            const auto primary = specialized_template(candidate);
            return physical_offset(primary.value_or(candidate));
        };
        const bool verified =
            std::ranges::find(request.selection.verified_offsets, declared_offset(cursor)) !=
                request.selection.verified_offsets.end() ||
            std::ranges::any_of(collector.selected, [&](CXCursor candidate) {
                return clang_equalCursors(canonical, clang_getCanonicalCursor(candidate)) &&
                       std::ranges::find(request.selection.verified_offsets, declared_offset(candidate)) !=
                           request.selection.verified_offsets.end();
            });
        if (!verified) {
            result.has_errors = true;
            result.diagnostics.push_back(
                {Severity::Error,
                 "ordinary function '" + qualified_name_of(cursor) + "' return cannot establish refinement '" +
                     *refined + "'; verify its definition (explicit trusted refinement boundaries are not implemented)",
                 presumed_location(clang_getCursorLocation(cursor))});
        }
    }
    for (const auto declaration : collector.unverified_storage) {
        if (request.selection.refinements.empty())
            break;
        if (const auto refined = refinement_use(declaration, request.selection)) {
            result.has_errors = true;
            result.diagnostics.push_back(
                {Severity::Error,
                 "storage '" + take(clang_getCursorSpelling(declaration)) + "' uses refinement '" + *refined +
                     "' outside a modeled verified body, where ordinary C++ could establish it without proof; a "
                     "verified body checks its own construction and writes, but an unverified construction boundary "
                     "is not yet checked",
                 presumed_location(clang_getCursorLocation(declaration))});
        }
    }

    // The memory capabilities each verified function's contract states, keyed by
    // the analysis offset of the declaration they belong to.
    //
    // They are collected before any body is lowered because a probe is an
    // ordinary function of this unit and may be parsed after the body it
    // constrains. A body may rely only on what its own contract states
    // (SPEC.md VERIFIED-043).
    // Keyed by the analysis offset of the verified function the clause belongs
    // to, so a body may rely only on its own contract.
    std::unordered_map<std::size_t, std::vector<StatedCapability>> stated_capabilities;
    for (const auto& probe : request.selection.proposition_probes) {
        if (probe.shape.kind != source::ProjectionKind::Readable &&
            probe.shape.kind != source::ProjectionKind::Writable &&
            probe.shape.kind != source::ProjectionKind::Capabilities && !mixes_capabilities(probe.shape)) {
            continue;
        }
        const auto at = std::ranges::find_if(collector.functions, [&](CXCursor candidate) {
            return take(clang_getCursorSpelling(candidate)) == probe.name;
        });
        if (at == collector.functions.end()) {
            continue;
        }
        Function resolved;
        // A member function's probe is a member too, so a pointer it names
        // stands past the implicit object's leaves (SPEC.md CLASS-008).
        MemberStanding standing = member_standing(*at, request.selection.refinements);
        extract_formal(resolved, *at, Signature{parameters_of(*at), std::move(standing.receiver), true}, probe.shape);
        if (resolved.capabilities.empty()) {
            continue;
        }
        // The probe's parameters mirror the verified function's, so the index
        // each capability resolved against is the function's own parameter.
        const auto owner = std::ranges::find_if(request.selection.clause_owners,
                                                [&](const auto& candidate) { return candidate.probe == probe.owner; });
        if (owner == request.selection.clause_owners.end()) {
            continue;
        }
        for (const Capability& capability : resolved.capabilities) {
            StatedCapability stated;
            stated.parameter = capability.pointer.root.id;
            stated.kind = capability.kind;
            stated.extent = capability.extent;
            stated_capabilities[owner->function_offset].push_back(stated);
        }
    }

    for (const CXCursor& cursor : collector.selected) {
        Function function;
        function.usr = take(clang_getCursorUSR(cursor));
        function.external_linkage = clang_getCursorLinkage(cursor) == CXLinkage_External;
        function.name = take(clang_getCursorSpelling(cursor));
        function.qualified_name = qualified_name_of(cursor);
        // A projected proof expression returns `decltype(auto)` over a
        // parenthesized expression, so Clang gives it a reference type whenever
        // the expression is a glvalue. That reference is an artifact of how the
        // expression is handed to Clang, not something the author wrote, and the
        // value denoted is the subject's own. An ordinary declaration's result
        // and parameters keep reference types opaque, so a contract is never
        // proven about a value another object can change (AGENTS.md 11).
        const bool projected_expression = !request.selection.specification_prefix.empty() &&
                                          function.name.starts_with(request.selection.specification_prefix);
        function.result = convert_type(clang_getCursorResultType(cursor), 0,
                                       projected_expression ? ReferenceModel::Referent : ReferenceModel::Opaque,
                                       &request.selection.refinements);
        function.location = presumed_location(clang_getCursorLocation(cursor));
        function.analysis_offset = physical_offset(cursor);
        // A specialization records the template it came from and the arguments
        // it was instantiated at. Its `usr` already differs per specialization,
        // so this identifies which declaration's contract it carries without
        // ever merging two of them (SPEC.md TEMPLATE-001, TEMPLATE-003).
        if (const auto primary = specialized_template(cursor); primary.has_value()) {
            function.primary_usr = take(clang_getCursorUSR(*primary));
            function.template_arguments = template_arguments_of(cursor);
            // An implicit instantiation carries the contract written on the
            // primary, so it is keyed to the primary's declaration: the
            // contract written once is found for every specialization of it.
            //
            // An explicit specialization states its own contract at its own
            // location, and the projector recorded that declaration rather
            // than the primary's. Re-keying it to the primary would hand it a
            // contract written for a different body and would collide with the
            // primary's own declaration (SPEC.md TEMPLATE-001, TEMPLATE-003).
            //
            // An implicit instantiation reports the primary's own location,
            // while an explicit specialization is written somewhere else and
            // reports that. Comparing the two is what separates them: the C
            // API exposes no specialization-kind predicate.
            const bool states_own_contract =
                physical_offset(cursor) != physical_offset(*primary) &&
                std::ranges::find(request.selection.verified_offsets, physical_offset(cursor)) !=
                    request.selection.verified_offsets.end();
            if (!states_own_contract) {
                function.analysis_offset = physical_offset(*primary);
                function.location = presumed_location(clang_getCursorLocation(*primary));
            }
        }

        // A refinement on a parameter or a result is verification-level identity
        // Clang canonicalizes away, so it is recovered from the written type here
        // (SPEC.md 17.3): a refined parameter carries its predicate into the body,
        // and a refined result states one at every return.
        const auto attach_refinements = [&](Type& type, CXCursor declaration, CXType written) {
            auto resolved = refinements_of(declaration, written, request.selection.refinements);
            if (resolved) {
                type.refinements = std::move(*resolved);
            } else {
                result.has_errors = true;
                result.diagnostics.push_back(
                    {Severity::Error, resolved.error(), presumed_location(clang_getCursorLocation(declaration))});
            }
        };
        attach_refinements(function.result, cursor, clang_getCursorResultType(cursor));

        // A member function's implicit object: one reference parameter per
        // leaf, standing before the written ones (SPEC.md CLASS-008). One this
        // implementation does not verify is refused by name, and its body is
        // not lowered as though it were verified without its object.
        const bool executable = std::ranges::find(request.selection.verified_offsets, function.analysis_offset) !=
                                request.selection.verified_offsets.end();
        MemberStanding standing = member_standing(cursor, request.selection.refinements);
        if (standing.rejection.has_value()) {
            if (executable) {
                function.member_rejection = std::move(standing.rejection);
            } else {
                function.has_body = true;
                function.body_rejection = std::move(standing.rejection);
            }
            result.functions.push_back(std::move(function));
            continue;
        }
        if (standing.receiver.has_value()) {
            for (const ReceiverLeaf& leaf : standing.receiver->leaves) {
                function.parameters.push_back(Parameter{leaf.spelling, leaf.type, standing.receiver->passing(leaf)});
            }
        }

        const std::vector<CXCursor> parameter_cursors = parameters_of(cursor);
        for (const CXCursor& parameter : parameter_cursors) {
            // A proof binder is projected as a reference parameter so Clang
            // resolves it without requiring a copy, a move, a default
            // constructor or any runtime object. It denotes the subject's own
            // value, so the referent is what it means.
            const auto written = clang_getCursorType(parameter);
            const auto passing = passing_of(written);
            Type parameter_type = convert_type(written, 0, ReferenceModel::Referent, &request.selection.refinements);
            attach_refinements(parameter_type, parameter,
                               source::aliases_storage(passing) ? reference_value_type(written) : written);
            function.parameters.push_back(
                Parameter{take(clang_getCursorSpelling(parameter)), std::move(parameter_type), passing});
        }

        // The projector's invariant declarations share the generated prefix,
        // which no ordinary declaration may use.
        const auto probe = std::ranges::find_if(request.selection.proposition_probes,
                                                [&](const auto& selected) { return selected.name == function.name; });
        if (probe != request.selection.proposition_probes.end()) {
            // A templated probe is reached through the reference that forced its
            // instantiation, which names a declaration; the proposition it
            // states lives in the definition. Clang owns which declaration is
            // the definition, so it is asked rather than assumed.
            const CXCursor defined = clang_getCursorDefinition(cursor);
            const CXCursor stating = clang_Cursor_isNull(defined) != 0 ? cursor : defined;
            extract_formal(function, stating, Signature{parameters_of(stating), std::move(standing.receiver), true},
                           probe->shape);
        } else {
            // Clang owns declaration/definition identity, including overloads
            // and parameter renaming. The public declaration supplies contract
            // metadata; the resolved definition supplies the executable body.
            const CXCursor definition = clang_getCursorDefinition(cursor);
            const CXCursor body_cursor = clang_Cursor_isNull(definition) ? cursor : definition;
            const auto stated = stated_capabilities.find(function.analysis_offset);
            // A body a verified function states is lowered as a body; anything
            // else selected here is a clause or a definition the formal core
            // may use, which reads a member of the implicit object as the
            // parameter it is.
            extract_body(
                function, body_cursor, Signature{parameters_of(body_cursor), std::move(standing.receiver), !executable},
                request.selection.specification_prefix.empty() ? std::string() : request.selection.specification_prefix,
                request.selection.refinements, executable,
                stated == stated_capabilities.end() ? nullptr : &stated->second);
        }
        result.functions.push_back(std::move(function));
    }

    return result;
}

std::string clang_version() {
    return take(clang_getClangVersion());
}

} // namespace cppl::clangbridge
