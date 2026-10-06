#include "proof_instantiation.hpp"

#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cctype>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/CXString.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::clangbridge::detail {

namespace {

// Types nest and records reach one another. A chain deeper than this is
// refused rather than followed (AGENTS.md 23).
constexpr unsigned kMaxDepth = 64;

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

// An alias template whose substitution only forms a type: no expression, no
// constraint and no default argument in it. The type it forms is checked where
// it is used, as any other.
bool inert_alias(CXCursor declaration) {
    return !states_constraint(declaration) && std::ranges::all_of(children_of(declaration), inert_alias_member);
}

// What proof-only text may instantiate: `key` names the template it comes
// from, so a unit is told once per template, and `description` says what it is.
struct Hazard {
    std::string key;
    std::string description;
};

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

class Checker {
  public:
    Checker(CXTranslationUnit unit, const Selection& selection)
        : spans_(selection.proof_only),
          prefix_(selection.specification_prefix),
          defined_probes_(selection.defined_clause_probes),
          translation_unit_(unit) {}

    // Every proof-only construct under `root`.
    void walk(CXCursor root) {
        clang_visitChildren(root, &Checker::visit, this);
    }

    // A specialization reached from its use. Its body stands at the pattern's
    // offsets, so the proof-only text in it is recognized as in the pattern.
    void walk_specialization(CXCursor specialization) {
        if (contract_probe(spelling(specialization))) {
            enter_probe(specialization);
            return;
        }
        walk(specialization);
    }

    std::vector<Diagnostic> take_findings() {
        return std::move(findings_);
    }

  private:
    static CXChildVisitResult visit(CXCursor cursor, CXCursor, CXClientData data) {
        return static_cast<Checker*>(data)->enter(cursor);
    }

    void visit_subtree(CXCursor cursor) {
        if (enter(cursor) == CXChildVisit_Recurse) {
            clang_visitChildren(cursor, &Checker::visit, this);
        }
    }

    [[nodiscard]] bool proof_only(std::size_t offset) const {
        const auto after = std::ranges::upper_bound(spans_, offset, {}, &source::ByteSpan::offset);
        return after != spans_.begin() && offset < std::prev(after)->end();
    }

    // Whether [begin, end] meets any proof-only span.
    [[nodiscard]] bool overlaps(std::size_t begin, std::size_t end) const {
        const auto first = std::ranges::upper_bound(spans_, begin, {}, &source::ByteSpan::end);
        return first != spans_.end() && first->offset <= end;
    }

    [[nodiscard]] bool generated_name(std::string_view name) const {
        return !prefix_.empty() && name.starts_with(prefix_);
    }

    // The projector's clause probes, whose parameters restate the verified
    // function's own.
    [[nodiscard]] bool contract_probe(std::string_view name) const {
        if (!generated_name(name)) {
            return false;
        }
        const std::string_view rest = name.substr(prefix_.size());
        return rest.starts_with("ensures_") || rest.starts_with("expects_") || rest.starts_with("decreases_");
    }

    // Whether a declaration, or a record enclosing it, is one the projector
    // generated. A formal namespace is not: what an author writes in a Law
    // stands in one.
    [[nodiscard]] bool generated(CXCursor declaration) const {
        CXCursor scope = declaration;
        for (unsigned depth = 0; depth < kMaxDepth && !is_null(scope); ++depth) {
            const CXCursorKind kind = kind_of(scope);
            if (kind == CXCursor_TranslationUnit || kind == CXCursor_Namespace) {
                return false;
            }
            if (generated_name(spelling(scope))) {
                return true;
            }
            scope = clang_getCursorSemanticParent(scope);
        }
        return false;
    }

    // The projector's own scaffolding, which holds nothing the author wrote: a
    // helper template it instantiates only at types read elsewhere, the
    // reference that makes a verified template instantiate its contract, the
    // reference that reaches what an explicit instantiation already
    // instantiated, and the alias a refinement lowers to in both programs.
    [[nodiscard]] bool scaffolding(CXCursorKind kind, std::string_view name, bool inside) const {
        if (generated_name(name)) {
            const std::string_view rest = name.substr(prefix_.size());
            if (rest.starts_with("force_") || rest.starts_with("instantiate_")) {
                return true;
            }
            if (kind == CXCursor_ClassTemplate || kind == CXCursor_ClassTemplatePartialSpecialization ||
                kind == CXCursor_FunctionTemplate || is_record(kind)) {
                return true;
            }
        }
        return inside && (kind == CXCursor_TypeAliasDecl || kind == CXCursor_TypedefDecl ||
                          kind == CXCursor_TypeAliasTemplateDecl);
    }

    CXChildVisitResult enter(CXCursor cursor) {
        const CXSourceRange extent = clang_getCursorExtent(cursor);
        const auto begin = main_file_offset(clang_getRangeStart(extent));
        const auto end = main_file_offset(clang_getRangeEnd(extent));
        if (begin.has_value() && end.has_value() && !overlaps(*begin, *end)) {
            return CXChildVisit_Continue;
        }
        const CXCursorKind kind = kind_of(cursor);
        const auto at = main_file_offset(clang_getCursorLocation(cursor));
        const bool inside = at.has_value() && proof_only(*at);
        if (clang_isDeclaration(kind) != 0) {
            const std::string name = spelling(cursor);
            if (contract_probe(name)) {
                enter_probe(cursor);
                return CXChildVisit_Continue;
            }
            if (scaffolding(kind, name, inside)) {
                return CXChildVisit_Continue;
            }
        }
        if (inside) {
            check(cursor, kind);
        }
        return CXChildVisit_Recurse;
    }

    // A clause probe restates the verified function's template header, its
    // constraints and its parameters, and declares a `result` of its return
    // type. The function's declaration stands before the probe in both
    // programs and names the same entities, so only the clause, the probe's
    // body, is the author's proof-only text. A parameter restates what the
    // program has completed already when it is a reference, which completes
    // nothing, or when the function is defined, which completes every
    // parameter and its return type. Otherwise the probe, a definition, would
    // complete it first, so it is checked as any other.
    void enter_probe(CXCursor probe) {
        const auto at = main_file_offset(clang_getCursorLocation(probe));
        if (at.has_value() && proof_only(*at)) {
            check(probe, kind_of(probe));
        }
        const bool defined = std::ranges::find(defined_probes_, spelling(probe)) != defined_probes_.end();
        for (const CXCursor child : children_of(probe)) {
            const CXCursorKind kind = kind_of(child);
            if (kind == CXCursor_ParmDecl) {
                const CXTypeKind passing = clang_getCanonicalType(clang_getCursorType(child)).kind;
                if (defined || passing == CXType_LValueReference || passing == CXType_RValueReference) {
                    continue;
                }
            } else if (kind != CXCursor_CompoundStmt) {
                continue;
            }
            visit_subtree(child);
        }
    }

    void check(CXCursor cursor, CXCursorKind kind) {
        if (clang_isExpression(kind) != 0) {
            report(cursor, type_hazard(clang_getCursorType(cursor), 0));
            const CXCursor referenced = clang_getCursorReferenced(cursor);
            if (!is_null(referenced) && clang_equalCursors(referenced, cursor) == 0) {
                report(cursor, reference_hazard(referenced, 0));
            }
            if (kind == CXCursor_CallExpr) {
                check_call(cursor, referenced);
            } else if (kind == CXCursor_BinaryOperator || kind == CXCursor_CompoundAssignOperator ||
                       kind == CXCursor_UnaryOperator || kind == CXCursor_ArraySubscriptExpr ||
                       kind == CXCursor_CXXNewExpr || kind == CXCursor_CXXDeleteExpr) {
                check_operator(cursor, kind);
            }
            return;
        }
        switch (kind) {
            case CXCursor_VarDecl:
            case CXCursor_ParmDecl:
            case CXCursor_FieldDecl:
                report(cursor, type_hazard(clang_getCursorType(cursor), 0));
                return;
            case CXCursor_FunctionDecl:
            case CXCursor_CXXMethod:
            case CXCursor_FunctionTemplate:
            case CXCursor_ConversionFunction:
                report(cursor, type_hazard(clang_getCursorResultType(cursor), 0));
                return;
            case CXCursor_TypeRef:
            case CXCursor_TemplateRef:
            case CXCursor_MemberRef:
            case CXCursor_VariableRef:
            case CXCursor_OverloadedDeclRef:
                report(cursor, reference_hazard(clang_getCursorReferenced(cursor), 0));
                return;
            default:
                return;
        }
    }

    // A call's overload resolution considers every function its name finds,
    // and deducing a function template's arguments can instantiate what its
    // signature names, whichever function is then selected. A member call
    // considers the class's members of that name; any other call, and every
    // operator, the namespace-scope functions and friends of that name too,
    // which is wider than what lookup finds.
    void check_call(CXCursor call, CXCursor referenced) {
        const std::string name = spelling(call);
        if (name.empty()) {
            return; // a dependent call's overload set is a cursor of its own
        }
        const bool member =
            !is_null(referenced) && is_function(kind_of(referenced)) && kind_of(referenced) != CXCursor_FunctionDecl;
        if (member) {
            const CXCursor owner = clang_getCursorSemanticParent(referenced);
            report(call, declaration_hazard(owner, clang_getCursorType(owner), 0));
            report(call,
                   member_candidates(owner, [&name](std::string_view candidate) { return candidate == name; }, 0));
            if (!is_operator_name(name)) {
                return;
            }
        }
        report(call, namespace_candidates([&name](std::string_view candidate) { return candidate == name; }));
    }

    // A built-in operator with an operand of class or enumeration type was
    // chosen by overload resolution over every operator function in scope.
    void check_operator(CXCursor expression, CXCursorKind kind) {
        bool overloaded = kind == CXCursor_CXXNewExpr || kind == CXCursor_CXXDeleteExpr;
        std::vector<CXCursor> records;
        for (CXCursor operand : children_of(expression)) {
            // An operand is read before the conversions C++ applies to it, which
            // libclang wraps it in: an enumeration promoted to an integer was an
            // enumeration when overload resolution saw it.
            for (unsigned depth = 0; depth < kMaxDepth && clang_isExpression(kind_of(operand)) != 0; ++depth) {
                const CXType type = clang_getCanonicalType(clang_getNonReferenceType(clang_getCursorType(operand)));
                if (type.kind == CXType_Record || type.kind == CXType_Enum || type.kind == CXType_Unexposed) {
                    overloaded = true;
                }
                if (type.kind == CXType_Record) {
                    records.push_back(clang_getTypeDeclaration(type));
                }
                const CXCursorKind wrapper = kind_of(operand);
                const std::vector<CXCursor> inner = children_of(operand);
                if ((wrapper != CXCursor_UnexposedExpr && wrapper != CXCursor_ParenExpr) || inner.size() != 1) {
                    break;
                }
                operand = inner.front();
            }
        }
        if (!overloaded) {
            return;
        }
        report(expression, namespace_candidates(is_operator_name));
        for (const CXCursor record : records) {
            report(expression, member_candidates(record, is_operator_name, 0));
        }
    }

    std::optional<Hazard> namespace_candidates(const std::function<bool(std::string_view)>& matches) {
        inventory();
        for (const auto& [name, functions] : functions_) {
            if (!matches(name)) {
                continue;
            }
            for (const CXCursor function : functions) {
                if (kind_of(function) == CXCursor_FunctionTemplate) {
                    return Hazard{usr(function), "'" + qualified(function) +
                                                     "', a template declared outside the standard library that "
                                                     "overload resolution for '" +
                                                     name + "' considers"};
                }
                if (auto hazard = type_hazard(clang_getCursorType(function), 1)) {
                    return considered(std::move(hazard), function, name);
                }
            }
        }
        return std::nullopt;
    }

    std::optional<Hazard> member_candidates(CXCursor owner, const std::function<bool(std::string_view)>& matches,
                                            unsigned depth) {
        if (depth > kMaxDepth) {
            return Hazard{"depth", "a class hierarchy deeper than this implementation follows"};
        }
        if (is_null(owner) || standard(owner) || generated(owner)) {
            return std::nullopt;
        }
        const CXCursor definition = clang_getCursorDefinition(owner);
        if (is_null(definition)) {
            return std::nullopt;
        }
        for (const CXCursor member : children_of(definition)) {
            const CXCursorKind kind = kind_of(member);
            if (kind == CXCursor_CXXBaseSpecifier) {
                const CXType base = clang_getCanonicalType(clang_getCursorType(member));
                if (auto hazard = member_candidates(clang_getTypeDeclaration(base), matches, depth + 1)) {
                    return hazard;
                }
                continue;
            }
            if (!matches(spelling(member))) {
                continue;
            }
            if (kind == CXCursor_FunctionTemplate) {
                return Hazard{usr(member), "'" + qualified(member) + "', a member template"};
            }
            if (is_function(kind)) {
                if (auto hazard = type_hazard(clang_getCursorType(member), depth + 1)) {
                    return considered(std::move(hazard), member, spelling(member));
                }
            }
        }
        return std::nullopt;
    }

    // Every function and function template outside the implementation that a
    // call could find by its name: those of a namespace, and every friend a
    // class declares, which argument-dependent lookup finds. Partial
    // specializations are noted on the way.
    void inventory() {
        if (inventoried_) {
            return;
        }
        inventoried_ = true;
        clang_visitChildren(
            clang_getTranslationUnitCursor(translation_unit_),
            [](CXCursor cursor, CXCursor, CXClientData data) {
                auto& self = *static_cast<Checker*>(data);
                if (clang_Location_isInSystemHeader(clang_getCursorLocation(cursor)) != 0) {
                    return CXChildVisit_Continue;
                }
                const CXCursorKind kind = kind_of(cursor);
                if (kind == CXCursor_ClassTemplatePartialSpecialization) {
                    const CXCursor primary = clang_getSpecializedCursorTemplate(cursor);
                    if (!is_null(primary)) {
                        self.partially_specialized_.insert(usr(primary));
                    }
                    return CXChildVisit_Recurse;
                }
                if (kind == CXCursor_Namespace || kind == CXCursor_LinkageSpec || kind == CXCursor_UnexposedDecl ||
                    is_record(kind) || kind == CXCursor_ClassTemplate || kind == CXCursor_FriendDecl) {
                    return CXChildVisit_Recurse;
                }
                if (kind == CXCursor_FunctionDecl || kind == CXCursor_FunctionTemplate) {
                    const CXCursor scope = clang_getCursorSemanticParent(cursor);
                    const CXCursorKind scope_kind = kind_of(scope);
                    const std::string name = spelling(cursor);
                    if ((scope_kind == CXCursor_Namespace || scope_kind == CXCursor_TranslationUnit ||
                         scope_kind == CXCursor_LinkageSpec) &&
                        !self.generated_name(name)) {
                        self.functions_[name].push_back(cursor);
                    }
                }
                return CXChildVisit_Continue;
            },
            this);
    }

    // A declaration a proof-only reference names.
    std::optional<Hazard> reference_hazard(CXCursor referenced, unsigned depth) {
        if (depth > kMaxDepth) {
            return Hazard{"depth", "a declaration nested more deeply than this implementation follows"};
        }
        if (is_null(referenced)) {
            return std::nullopt;
        }
        const CXCursorKind kind = kind_of(referenced);
        if (kind == CXCursor_OverloadedDeclRef) {
            const unsigned count = clang_getNumOverloadedDecls(referenced);
            for (unsigned index = 0; index < count; ++index) {
                if (auto hazard = reference_hazard(clang_getOverloadedDecl(referenced, index), depth + 1)) {
                    return hazard;
                }
            }
            return std::nullopt;
        }
        if (is_template_parameter(kind) || kind == CXCursor_Namespace || kind == CXCursor_NamespaceAlias ||
            kind == CXCursor_TranslationUnit || kind == CXCursor_ParmDecl || kind == CXCursor_LabelStmt) {
            return std::nullopt;
        }
        if (standard(referenced) || generated(referenced)) {
            return implementation_hazard(referenced, depth);
        }
        if (is_template(kind)) {
            return template_hazard(referenced);
        }
        // A variable template, and each of its specializations, is a
        // declaration libclang does not expose. One that is a template, or a
        // specialization by its identity, is refused as one.
        if (kind == CXCursor_UnexposedDecl) {
            const bool templated = usr(referenced).find(">#") != std::string::npos ||
                                   std::ranges::any_of(children_of(referenced), [](CXCursor child) {
                                       return is_template_parameter(kind_of(child));
                                   });
            if (templated) {
                return Hazard{usr(referenced), "'" + qualified(referenced) +
                                                   "', a variable template or its specialization, declared outside "
                                                   "the standard library"};
            }
            return type_hazard(clang_getCursorType(referenced), depth + 1);
        }
        if (is_record(kind) || kind == CXCursor_EnumDecl) {
            return declaration_hazard(referenced, clang_getCursorType(referenced), depth + 1);
        }
        const CXCursor pattern = clang_getSpecializedCursorTemplate(referenced);
        if (!is_null(pattern) && kind_of(pattern) == CXCursor_FunctionTemplate && !standard(pattern) &&
            !generated(pattern)) {
            return outside_template(pattern, "a specialization of '" + qualified(pattern) + "'");
        }
        if (auto hazard = member_hazard(referenced, depth)) {
            return hazard;
        }
        return type_hazard(clang_getCursorType(referenced), depth + 1);
    }

    // A declaration of the implementation, or one the projector generated, is
    // trusted to mean what it means wherever it is instantiated (TRUST.md
    // TCB-SOURCE-010). What it is instantiated at is the program's, and is
    // checked: its signature, its type, and every specialization enclosing it.
    std::optional<Hazard> implementation_hazard(CXCursor declaration, unsigned depth) {
        const CXCursorKind kind = kind_of(declaration);
        if (is_record(kind) || kind == CXCursor_EnumDecl) {
            return declaration_hazard(declaration, clang_getCursorType(declaration), depth + 1);
        }
        if (!is_template(kind)) {
            if (auto hazard = type_hazard(clang_getCursorType(declaration), depth + 1)) {
                return hazard;
            }
            const int arguments = clang_Cursor_getNumTemplateArguments(declaration);
            for (int index = 0; index < arguments; ++index) {
                const auto position = static_cast<unsigned>(index);
                if (clang_Cursor_getTemplateArgumentKind(declaration, position) != CXTemplateArgumentKind_Type) {
                    continue;
                }
                if (auto hazard = type_hazard(clang_Cursor_getTemplateArgumentType(declaration, position), depth + 1)) {
                    return hazard;
                }
            }
        }
        CXCursor scope = clang_getCursorSemanticParent(declaration);
        for (unsigned level = 0; level < kMaxDepth && !is_null(scope) && is_record(kind_of(scope)); ++level) {
            if (auto hazard = arguments_hazard(clang_getCursorType(scope), depth + 1)) {
                return hazard;
            }
            scope = clang_getCursorSemanticParent(scope);
        }
        return std::nullopt;
    }

    // A member of a specialization of a template outside the implementation.
    // Reading a data member, a member type or an enumerator instantiates the
    // specialization, which is judged as a whole (`declaration_hazard`); any
    // other member is instantiated on its own where it is used.
    std::optional<Hazard> member_hazard(CXCursor member, unsigned depth) {
        const CXCursorKind kind = kind_of(member);
        const bool data = kind == CXCursor_FieldDecl || kind == CXCursor_TypedefDecl ||
                          kind == CXCursor_TypeAliasDecl || kind == CXCursor_EnumConstantDecl ||
                          kind == CXCursor_EnumDecl;
        CXCursor scope = clang_getCursorSemanticParent(member);
        for (unsigned level = 0; level < kMaxDepth && !is_null(scope); ++level) {
            const CXCursorKind scope_kind = kind_of(scope);
            if (!is_record(scope_kind) && scope_kind != CXCursor_EnumDecl) {
                return std::nullopt;
            }
            const CXCursor pattern = clang_getSpecializedCursorTemplate(scope);
            if (is_record(scope_kind) && !is_null(pattern) && !standard(pattern) && !generated(pattern)) {
                const CXType type = clang_getCursorType(scope);
                if (data) {
                    return declaration_hazard(scope, type, depth + 1);
                }
                return outside_template(pattern, "'" + spelling(member) + "', a member of '" + named(type) +
                                                     "', a specialization of '" + qualified(template_of(pattern)) +
                                                     "'");
            }
            scope = clang_getCursorSemanticParent(scope);
        }
        return std::nullopt;
    }

    // A template outside the implementation, named. Naming a class template
    // is also what deducing its arguments starts from, which deduces every
    // guide written for it, so an inert one with a guide is refused where its
    // name is written, though a value of its specializations is not.
    std::optional<Hazard> template_hazard(CXCursor declaration) {
        if (standard(declaration) || generated(declaration)) {
            return std::nullopt;
        }
        const CXCursorKind kind = kind_of(declaration);
        if (kind == CXCursor_ClassTemplate && inert(declaration) && !guided(declaration)) {
            return std::nullopt;
        }
        if (kind == CXCursor_TypeAliasTemplateDecl && inert_alias(declaration)) {
            return std::nullopt;
        }
        return Hazard{usr(declaration),
                      "'" + qualified(declaration) + "', a template declared outside the standard library"};
    }

    // Whether a deduction guide is written for the class template.
    bool guided(CXCursor declaration) {
        inventory();
        return functions_.contains("<deduction guide for " + spelling(declaration) + ">");
    }

    // Whether instantiating a class template can change nothing outside the
    // specialization and mean the same wherever it happens: one defined at
    // namespace scope as data members, member types and member enumerations
    // alone, with no expression in it but an enumerator's built-in arithmetic
    // on literals, no base, no friend, no member function, no constraint and
    // no default template argument, and with no partial specialization a later
    // declaration could make a use select. What it holds is then fixed by its
    // arguments, which are checked where it is used. A template never defined
    // is never instantiated.
    bool inert(CXCursor declaration) {
        const std::string key = usr(declaration);
        if (const auto known = inert_.find(key); known != inert_.end()) {
            return known->second;
        }
        bool result = true;
        inventory();
        const CXCursorKind scope = kind_of(clang_getCursorSemanticParent(declaration));
        const CXCursor definition = clang_getCursorDefinition(declaration);
        if (partially_specialized_.contains(key) ||
            (scope != CXCursor_Namespace && scope != CXCursor_TranslationUnit && scope != CXCursor_LinkageSpec)) {
            result = false;
        } else if (!is_null(definition)) {
            result = !states_constraint(definition) && std::ranges::all_of(children_of(definition), inert_member);
        }
        inert_.emplace(key, result);
        return result;
    }

    // What proof-only text may instantiate through a value or a declaration of
    // `type`: every type it is built from.
    std::optional<Hazard> type_hazard(CXType type, unsigned depth) {
        if (depth > kMaxDepth) {
            return Hazard{"depth", "a type nested more deeply than this implementation follows"};
        }
        const CXType canonical = clang_getCanonicalType(type);
        switch (canonical.kind) {
            case CXType_Pointer:
            case CXType_LValueReference:
            case CXType_RValueReference:
            case CXType_BlockPointer:
                return type_hazard(clang_getPointeeType(canonical), depth + 1);
            case CXType_ConstantArray:
            case CXType_IncompleteArray:
            case CXType_VariableArray:
            case CXType_DependentSizedArray:
                return type_hazard(clang_getArrayElementType(canonical), depth + 1);
            case CXType_Vector:
            case CXType_ExtVector:
            case CXType_Complex:
                return type_hazard(clang_getElementType(canonical), depth + 1);
            case CXType_MemberPointer:
                if (auto hazard = type_hazard(clang_Type_getClassType(canonical), depth + 1)) {
                    return hazard;
                }
                return type_hazard(clang_getPointeeType(canonical), depth + 1);
            case CXType_FunctionProto:
            case CXType_FunctionNoProto: {
                if (auto hazard = type_hazard(clang_getResultType(canonical), depth + 1)) {
                    return hazard;
                }
                const int count = clang_getNumArgTypes(canonical);
                for (int index = 0; index < count; ++index) {
                    if (auto hazard =
                            type_hazard(clang_getArgType(canonical, static_cast<unsigned>(index)), depth + 1)) {
                        return hazard;
                    }
                }
                return std::nullopt;
            }
            case CXType_Atomic:
                return type_hazard(clang_Type_getValueType(canonical), depth + 1);
            case CXType_Record:
            case CXType_Enum:
            case CXType_Unexposed: {
                const CXCursor declaration = clang_getTypeDeclaration(canonical);
                if (is_null(declaration)) {
                    return std::nullopt;
                }
                return declaration_hazard(declaration, canonical, depth + 1);
            }
            default:
                return std::nullopt;
        }
    }

    // The type arguments of a specialization, and of every specialization it
    // is a member of.
    std::optional<Hazard> arguments_hazard(CXType type, unsigned depth) {
        const CXType canonical = clang_getCanonicalType(type);
        const int count = clang_Type_getNumTemplateArguments(canonical);
        for (int index = 0; index < count; ++index) {
            const CXType argument = clang_Type_getTemplateArgumentAsType(canonical, static_cast<unsigned>(index));
            if (argument.kind == CXType_Invalid) {
                continue;
            }
            if (auto hazard = type_hazard(argument, depth + 1)) {
                return hazard;
            }
        }
        return std::nullopt;
    }

    // What naming a value of the class, enumeration or template `declaration`
    // may instantiate. A record's verdict is kept, except one reached while a
    // record it depends on was still being decided: a class can reach itself
    // through a pointer member, and the verdict found inside that cycle is
    // only provisional.
    std::optional<Hazard> declaration_hazard(CXCursor declaration, CXType type, unsigned depth) {
        if (depth > kMaxDepth) {
            return Hazard{"depth", "a type nested more deeply than this implementation follows"};
        }
        const CXCursorKind kind = kind_of(declaration);
        if (is_template_parameter(kind)) {
            return std::nullopt;
        }
        if (is_template(kind)) {
            return template_hazard(declaration); // a dependent specialization
        }
        if (kind == CXCursor_EnumDecl) {
            return member_hazard(declaration, depth);
        }
        if (!is_record(kind)) {
            return std::nullopt;
        }
        std::string key = usr(declaration);
        if (key.empty()) {
            key = type_spelling(clang_getCanonicalType(type));
        }
        if (const auto known = hazards_.find(key); known != hazards_.end()) {
            return known->second;
        }
        if (harmless_.contains(key)) {
            return std::nullopt;
        }
        if (deciding_.contains(key)) {
            ++provisional_;
            return std::nullopt;
        }
        deciding_.insert(key);
        const std::size_t provisional = provisional_;
        std::optional<Hazard> result = record_hazard(declaration, type, depth);
        deciding_.erase(key);
        if (result.has_value()) {
            hazards_.emplace(key, *result);
        } else if (provisional == provisional_) {
            harmless_.insert(key);
        }
        return result;
    }

    std::optional<Hazard> record_hazard(CXCursor declaration, CXType type, unsigned depth) {
        if (standard(declaration) || generated(declaration)) {
            if (auto hazard = arguments_hazard(type, depth)) {
                return hazard;
            }
            return implementation_hazard_scopes(declaration, depth);
        }
        const CXCursor pattern = clang_getSpecializedCursorTemplate(declaration);
        if (!is_null(pattern) && !standard(pattern) && !generated(pattern)) {
            const std::string name = named(type);
            const CXCursor definition = clang_getCursorDefinition(declaration);
            // A specialization the author wrote out is a class like any other.
            // An implicit instantiation reports the pattern's location and
            // exposes no member as a cursor, so it is never mistaken for one.
            const bool written =
                !is_null(definition) &&
                clang_equalLocations(clang_getCursorLocation(declaration), clang_getCursorLocation(pattern)) == 0 &&
                !children_of(definition).empty();
            if (written) {
                if (auto hazard = arguments_hazard(type, depth)) {
                    return hazard;
                }
                return class_hazard(declaration, name, depth);
            }
            if (kind_of(pattern) == CXCursor_ClassTemplate && inert(pattern)) {
                if (auto hazard = arguments_hazard(type, depth)) {
                    return through(std::move(hazard), name);
                }
                return fields_hazard(type, name, depth);
            }
            return outside_template(pattern,
                                    "'" + name + "', a specialization of '" + qualified(template_of(pattern)) + "'");
        }
        if (auto hazard = member_hazard(declaration, depth)) {
            return hazard;
        }
        return class_hazard(declaration, named(type), depth);
    }

    // The specializations an implementation's record is nested in.
    std::optional<Hazard> implementation_hazard_scopes(CXCursor declaration, unsigned depth) {
        CXCursor scope = clang_getCursorSemanticParent(declaration);
        for (unsigned level = 0; level < kMaxDepth && !is_null(scope) && is_record(kind_of(scope)); ++level) {
            if (auto hazard = arguments_hazard(clang_getCursorType(scope), depth + 1)) {
                return hazard;
            }
            scope = clang_getCursorSemanticParent(scope);
        }
        return std::nullopt;
    }

    // The data members an instantiation holds, at the types it holds them.
    std::optional<Hazard> fields_hazard(CXType type, const std::string& name, unsigned depth) {
        std::vector<CXCursor> fields;
        clang_Type_visitFields(
            clang_getCanonicalType(type),
            [](CXCursor field, CXClientData data) {
                static_cast<std::vector<CXCursor>*>(data)->push_back(field);
                return CXVisit_Continue;
            },
            &fields);
        for (const CXCursor field : fields) {
            if (auto hazard = type_hazard(clang_getCursorType(field), depth + 1)) {
                return through(std::move(hazard), name);
            }
        }
        return std::nullopt;
    }

    // What using a value of a class may instantiate without any use of it
    // naming more: its bases, its data members, each constructor, conversion,
    // destructor and assignment it may call implicitly, and every template it
    // declares, a member's or a friend's, which overload resolution may
    // deduce. A class not defined in this unit has nothing to instantiate.
    std::optional<Hazard> class_hazard(CXCursor declaration, const std::string& name, unsigned depth) {
        const CXCursor definition = clang_getCursorDefinition(declaration);
        if (is_null(definition)) {
            return std::nullopt;
        }
        for (const CXCursor member : children_of(definition)) {
            const CXCursorKind kind = kind_of(member);
            switch (kind) {
                case CXCursor_FunctionTemplate:
                case CXCursor_ClassTemplate:
                    return Hazard{usr(member), "'" + qualified(member) + "', a member template of '" + name + "'"};
                case CXCursor_FriendDecl:
                    for (const CXCursor befriended : children_of(member)) {
                        if (is_template(kind_of(befriended))) {
                            return Hazard{usr(befriended),
                                          "'" + qualified(befriended) + "', a template '" + name + "' befriends"};
                        }
                    }
                    break;
                case CXCursor_Constructor:
                case CXCursor_ConversionFunction:
                case CXCursor_Destructor:
                case CXCursor_CXXBaseSpecifier:
                case CXCursor_FieldDecl:
                    if (auto hazard = type_hazard(clang_getCursorType(member), depth + 1)) {
                        return through(std::move(hazard), name);
                    }
                    break;
                case CXCursor_CXXMethod:
                    if (spelling(member) == "operator=") {
                        if (auto hazard = type_hazard(clang_getCursorType(member), depth + 1)) {
                            return through(std::move(hazard), name);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        return std::nullopt;
    }

    void report(CXCursor at, std::optional<Hazard> hazard) {
        if (!hazard.has_value() || !reported_.insert(hazard->key).second) {
            return;
        }
        findings_.push_back(Diagnostic{Severity::Error,
                                       "proof-only text may instantiate " + hazard->description +
                                           ", where the program run does not, so the program verified would not "
                                           "be the program run (SPEC.md ERASE-019)",
                                       presumed_location(at)});
    }

    const std::vector<source::ByteSpan>& spans_;
    const std::string& prefix_;
    const std::vector<std::string>& defined_probes_;
    CXTranslationUnit translation_unit_;

    std::vector<Diagnostic> findings_;
    std::set<std::string> reported_;

    bool inventoried_ = false;
    std::map<std::string, std::vector<CXCursor>> functions_;
    std::set<std::string> partially_specialized_;
    std::map<std::string, bool> inert_;

    std::map<std::string, Hazard> hazards_;
    std::set<std::string> harmless_;
    std::set<std::string> deciding_;
    std::size_t provisional_ = 0;
};

} // namespace

std::vector<Diagnostic> proof_only_instantiations(CXTranslationUnit unit, const Selection& selection,
                                                  const std::vector<CXCursor>& specializations) {
    if (selection.proof_only.empty()) {
        return {};
    }
    Checker checker(unit, selection);
    checker.walk(clang_getTranslationUnitCursor(unit));
    for (const CXCursor specialization : specializations) {
        checker.walk_specialization(specialization);
    }
    return checker.take_findings();
}

} // namespace cppl::clangbridge::detail
