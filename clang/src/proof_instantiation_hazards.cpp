// Whether what an instantiation names could reach proof-only code: the
// hazards of references, types, declarations and classes.

#include "cppl/clang/ast.hpp"
#include "proof_instantiation_checker.hpp"

#include <algorithm>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cppl::clangbridge::detail {

using instantiation::Checker;
using instantiation::children_of;
using instantiation::Hazard;
using instantiation::inert_alias;
using instantiation::inert_member;
using instantiation::is_null;
using instantiation::is_record;
using instantiation::is_template;
using instantiation::is_template_parameter;
using instantiation::kind_of;
using instantiation::kMaxDepth;
using instantiation::named;
using instantiation::outside_template;
using instantiation::presumed_location;
using instantiation::qualified;
using instantiation::spelling;
using instantiation::standard;
using instantiation::states_constraint;
using instantiation::template_of;
using instantiation::through;
using instantiation::type_spelling;
using instantiation::usr;

std::optional<Hazard> Checker::reference_hazard(CXCursor referenced, unsigned depth) {
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

std::optional<Hazard> Checker::implementation_hazard(CXCursor declaration, unsigned depth) {
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

std::optional<Hazard> Checker::member_hazard(CXCursor member, unsigned depth) {
    const CXCursorKind kind = kind_of(member);
    const bool data = kind == CXCursor_FieldDecl || kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl ||
                      kind == CXCursor_EnumConstantDecl || kind == CXCursor_EnumDecl;
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
                                                 "', a specialization of '" + qualified(template_of(pattern)) + "'");
        }
        scope = clang_getCursorSemanticParent(scope);
    }
    return std::nullopt;
}

std::optional<Hazard> Checker::template_hazard(CXCursor declaration) {
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

bool Checker::guided(CXCursor declaration) {
    inventory();
    return functions_.contains("<deduction guide for " + spelling(declaration) + ">");
}

bool Checker::inert(CXCursor declaration) {
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

std::optional<Hazard> Checker::type_hazard(CXType type, unsigned depth) {
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
                if (auto hazard = type_hazard(clang_getArgType(canonical, static_cast<unsigned>(index)), depth + 1)) {
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

std::optional<Hazard> Checker::arguments_hazard(CXType type, unsigned depth) {
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

std::optional<Hazard> Checker::declaration_hazard(CXCursor declaration, CXType type, unsigned depth) {
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

std::optional<Hazard> Checker::record_hazard(CXCursor declaration, CXType type, unsigned depth) {
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

std::optional<Hazard> Checker::implementation_hazard_scopes(CXCursor declaration, unsigned depth) {
    CXCursor scope = clang_getCursorSemanticParent(declaration);
    for (unsigned level = 0; level < kMaxDepth && !is_null(scope) && is_record(kind_of(scope)); ++level) {
        if (auto hazard = arguments_hazard(clang_getCursorType(scope), depth + 1)) {
            return hazard;
        }
        scope = clang_getCursorSemanticParent(scope);
    }
    return std::nullopt;
}

std::optional<Hazard> Checker::fields_hazard(CXType type, const std::string& name, unsigned depth) {
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

std::optional<Hazard> Checker::class_hazard(CXCursor declaration, const std::string& name, unsigned depth) {
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

void Checker::report(CXCursor at, std::optional<Hazard> hazard) {
    if (!hazard.has_value() || !reported_.insert(hazard->key).second) {
        return;
    }
    // Clang accepted the unit: it is refused because this implementation
    // cannot show its erasure keeps what it means (SPEC.md ERASE-019).
    findings_.push_back(Diagnostic{Severity::Error, Category::UnsupportedSemantics,
                                   "proof-only text may instantiate " + hazard->description +
                                       ", where the program run does not, so the program verified would not "
                                       "be the program run (SPEC.md ERASE-019)",
                                   presumed_location(at)});
}

} // namespace cppl::clangbridge::detail
