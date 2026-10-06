// What the instantiation check asks of Clang's cursors: their names,
// kinds and templates, the literals and constraints they state, and
// whether a member or an alias is inert.

#include "cppl/source/location.hpp"
#include "proof_instantiation_checker.hpp"

#include <algorithm>
#include <cctype>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/CXString.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::clangbridge::detail {

using instantiation::children_of;
using instantiation::kind_of;
using instantiation::kMaxDepth;

namespace {

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

} // namespace

namespace instantiation {

std::string spelling(CXCursor cursor) {
    return take(clang_getCursorSpelling(cursor));
}

std::string usr(CXCursor cursor) {
    return take(clang_getCursorUSR(cursor));
}

std::string type_spelling(CXType type) {
    return take(clang_getTypeSpelling(type));
}

// A type as a diagnostic names it: canonical, without its qualifiers.
std::string named(CXType type) {
    return type_spelling(clang_getUnqualifiedType(clang_getCanonicalType(type)));
}

bool is_null(CXCursor cursor) {
    return clang_Cursor_isNull(cursor) != 0 || clang_isInvalid(clang_getCursorKind(cursor)) != 0;
}

CXCursorKind kind_of(CXCursor cursor) {
    return clang_getCursorKind(cursor);
}

source::SourceLocation presumed_location(CXCursor cursor) {
    CXString file{};
    unsigned line = 0;
    unsigned column = 0;
    clang_getPresumedLocation(clang_getCursorLocation(cursor), &file, &line, &column);
    source::SourceLocation result;
    result.file = take(file);
    result.line = line;
    result.column = column;
    return result;
}

// Where a location stands in the buffer Clang parsed, when it stands there.
std::optional<std::size_t> main_file_offset(CXSourceLocation location) {
    if (clang_Location_isFromMainFile(location) == 0) {
        return std::nullopt;
    }
    unsigned offset = 0;
    clang_getFileLocation(location, nullptr, nullptr, nullptr, &offset);
    return static_cast<std::size_t>(offset);
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

bool is_record(CXCursorKind kind) {
    return kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl || kind == CXCursor_UnionDecl;
}

bool is_template(CXCursorKind kind) {
    return kind == CXCursor_ClassTemplate || kind == CXCursor_FunctionTemplate ||
           kind == CXCursor_ClassTemplatePartialSpecialization || kind == CXCursor_TypeAliasTemplateDecl ||
           kind == CXCursor_ConceptDecl;
}

bool is_template_parameter(CXCursorKind kind) {
    return kind == CXCursor_TemplateTypeParameter || kind == CXCursor_NonTypeTemplateParameter ||
           kind == CXCursor_TemplateTemplateParameter;
}

bool is_function(CXCursorKind kind) {
    return kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod || kind == CXCursor_Constructor ||
           kind == CXCursor_Destructor || kind == CXCursor_ConversionFunction;
}

} // namespace instantiation

namespace {

// Whether every child of `cursor` only names a type: what a declaration with
// no expression in it has. A default member initializer, a bit-field width, an
// array bound, a `decltype` operand or an expression template argument is an
// expression child, and is evaluated where the declaration is instantiated.
bool names_types_only(CXCursor cursor) {
    return std::ranges::all_of(children_of(cursor), [](CXCursor child) {
        const CXCursorKind kind = kind_of(child);
        return kind == CXCursor_TypeRef || kind == CXCursor_TemplateRef || kind == CXCursor_NamespaceRef;
    });
}

// Whether an enumerator's value is computed from literals, the enumeration's
// own enumerators and the template's parameters by built-in operators alone:
// nothing in it is looked up where the enumeration is instantiated.
bool literal_value(CXCursor expression, CXCursor enumeration, unsigned depth) {
    if (depth > kMaxDepth) {
        return false;
    }
    const std::vector<CXCursor> operands = children_of(expression);
    switch (clang_getCursorKind(expression)) {
        case CXCursor_IntegerLiteral:
        case CXCursor_CharacterLiteral:
        case CXCursor_CXXBoolLiteralExpr:
        case CXCursor_UnaryOperator:
        case CXCursor_BinaryOperator:
        case CXCursor_ParenExpr:
            break;
        case CXCursor_UnexposedExpr:
            // A conversion libclang does not expose wraps the one operand it
            // converts; anything else it does not expose is not known to be
            // a literal.
            if (operands.size() != 1) {
                return false;
            }
            break;
        case CXCursor_DeclRefExpr: {
            const CXCursor referenced = clang_getCursorReferenced(expression);
            const CXCursorKind kind = clang_getCursorKind(referenced);
            if (kind == CXCursor_NonTypeTemplateParameter) {
                break;
            }
            if (kind != CXCursor_EnumConstantDecl ||
                clang_equalCursors(clang_getCursorSemanticParent(referenced), enumeration) == 0) {
                return false;
            }
            break;
        }
        default:
            return false;
    }
    return std::ranges::all_of(operands,
                               [&](CXCursor operand) { return literal_value(operand, enumeration, depth + 1); });
}

// A member enumeration whose underlying type is only named and whose every
// enumerator is a `literal_value`.
bool literal_enumeration(CXCursor enumeration) {
    return std::ranges::all_of(children_of(enumeration), [&](CXCursor member) {
        switch (clang_getCursorKind(member)) {
            case CXCursor_TypeRef:
            case CXCursor_NamespaceRef:
                return true;
            case CXCursor_EnumConstantDecl:
                return std::ranges::all_of(children_of(member),
                                           [&](CXCursor value) { return literal_value(value, enumeration, 0); });
            default:
                return false;
        }
    });
}

// A name the implementation reserves (C++ [lex.name]).
bool reserved(std::string_view name) {
    return name.size() >= 2 && name[0] == '_' &&
           (name[1] == '_' || std::isupper(static_cast<unsigned char>(name[1])) != 0);
}

} // namespace

namespace instantiation {

bool is_operator_name(std::string_view name) {
    constexpr std::string_view word = "operator";
    return name.starts_with(word) && name.size() > word.size() &&
           std::isalnum(static_cast<unsigned char>(name[word.size()])) == 0 && name[word.size()] != '_';
}

// The name of a declaration with every scope enclosing it, for a diagnostic.
std::string qualified(CXCursor cursor) {
    std::vector<std::string> scopes{spelling(cursor)};
    CXCursor scope = clang_getCursorSemanticParent(cursor);
    for (unsigned depth = 0; depth < kMaxDepth && !is_null(scope) && kind_of(scope) != CXCursor_TranslationUnit;
         ++depth) {
        if (std::string outer = spelling(scope); !outer.empty()) {
            scopes.push_back(std::move(outer));
        }
        scope = clang_getCursorSemanticParent(scope);
    }
    std::string name;
    for (const std::string& outer : std::views::reverse(scopes)) {
        if (!name.empty()) {
            name += "::";
        }
        name += outer;
    }
    return name;
}

// The template a specialization's pattern belongs to: the pattern itself, or
// the template a member pattern stands in.
CXCursor template_of(CXCursor pattern) {
    CXCursor scope = pattern;
    for (unsigned depth = 0; depth < kMaxDepth && !is_null(scope) && kind_of(scope) != CXCursor_TranslationUnit;
         ++depth) {
        if (is_template(kind_of(scope))) {
            return scope;
        }
        scope = clang_getCursorSemanticParent(scope);
    }
    return pattern;
}

// Whether a declaration is the implementation's own: written in a system
// header, and in namespace `std` or in a namespace whose name the
// implementation reserves (`__gnu_cxx`), or, outside every namespace, either
// reserved by name or neither a template nor a specialization of one, as the C
// library's types and functions are. A library installed as a system header
// under a namespace of its own is not the implementation's, so its templates
// are held to the rule the program's are.
bool standard(CXCursor declaration) {
    if (clang_Location_isInSystemHeader(clang_getCursorLocation(declaration)) == 0) {
        return false;
    }
    std::optional<std::string> outermost;
    CXCursor top = declaration;
    for (unsigned depth = 0; depth < kMaxDepth; ++depth) {
        const CXCursor scope = clang_getCursorSemanticParent(top);
        if (is_null(scope) || kind_of(scope) == CXCursor_TranslationUnit) {
            break;
        }
        if (kind_of(scope) == CXCursor_Namespace) {
            outermost = spelling(scope);
        }
        top = scope;
    }
    if (outermost.has_value()) {
        return *outermost == "std" || reserved(*outermost);
    }
    return reserved(spelling(top)) || (!is_template(kind_of(top)) && is_null(clang_getSpecializedCursorTemplate(top)));
}

// Whether `cursor` contains the keyword `requires`: a constraint, whose
// satisfaction Clang decides once and keeps.
bool states_constraint(CXCursor cursor) {
    CXTranslationUnit unit = clang_Cursor_getTranslationUnit(cursor);
    CXToken* tokens = nullptr;
    unsigned count = 0;
    clang_tokenize(unit, clang_getCursorExtent(cursor), &tokens, &count);
    bool found = false;
    for (unsigned index = 0; index < count && !found; ++index) {
        found = clang_getTokenKind(tokens[index]) == CXToken_Keyword &&
                take(clang_getTokenSpelling(unit, tokens[index])) == "requires";
    }
    clang_disposeTokens(unit, tokens, count);
    return found;
}

// A member a class template may declare and stay inert (`Checker::inert`): a
// template parameter without a default, a data member or a member type that
// only names types, a member enumeration of literal values, an access
// specifier.
bool inert_member(CXCursor member) {
    switch (kind_of(member)) {
        case CXCursor_TemplateTypeParameter:
        case CXCursor_TemplateTemplateParameter:
            return children_of(member).empty();
        case CXCursor_NonTypeTemplateParameter:
        case CXCursor_FieldDecl:
        case CXCursor_TypedefDecl:
        case CXCursor_TypeAliasDecl:
            return names_types_only(member);
        case CXCursor_EnumDecl:
            return literal_enumeration(member);
        case CXCursor_CXXAccessSpecifier:
            return true;
        default:
            return false;
    }
}

} // namespace instantiation

namespace {

// What an inert alias template declares: template parameters without
// defaults, and the alias, which only names types.
bool inert_alias_member(CXCursor member) {
    switch (kind_of(member)) {
        case CXCursor_TemplateTypeParameter:
        case CXCursor_TemplateTemplateParameter:
            return children_of(member).empty();
        case CXCursor_NonTypeTemplateParameter:
        case CXCursor_TypeAliasDecl:
            return names_types_only(member);
        default:
            return false;
    }
}

} // namespace

namespace instantiation {

// An alias template whose substitution only forms a type: no expression, no
// constraint and no default argument in it. The type it forms is checked where
// it is used, as any other.
bool inert_alias(CXCursor declaration) {
    return !states_constraint(declaration) && std::ranges::all_of(children_of(declaration), inert_alias_member);
}

std::optional<Hazard> through(std::optional<Hazard> hazard, const std::string& carrier) {
    if (hazard.has_value()) {
        hazard->description += ", through '" + carrier + "'";
    }
    return hazard;
}

// A function overload resolution considers for a call by `name`.
std::optional<Hazard> considered(std::optional<Hazard> hazard, CXCursor function, const std::string& name) {
    if (hazard.has_value()) {
        hazard->description +=
            ", through '" + qualified(function) + "', which overload resolution for '" + name + "' considers";
    }
    return hazard;
}

Hazard outside_template(CXCursor pattern, const std::string& what) {
    const CXCursor owner = template_of(pattern);
    return Hazard{usr(owner), what + ", a template declared outside the standard library"};
}

} // namespace instantiation

} // namespace cppl::clangbridge::detail
