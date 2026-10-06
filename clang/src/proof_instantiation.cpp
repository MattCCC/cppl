#include "proof_instantiation.hpp"

#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/source/location.hpp"
#include "proof_instantiation_checker.hpp"

#include <algorithm>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::clangbridge::detail {

using instantiation::Checker;
using instantiation::children_of;
using instantiation::considered;
using instantiation::Hazard;
using instantiation::is_function;
using instantiation::is_null;
using instantiation::is_operator_name;
using instantiation::is_record;
using instantiation::kind_of;
using instantiation::kMaxDepth;
using instantiation::main_file_offset;
using instantiation::qualified;
using instantiation::spelling;
using instantiation::standard;
using instantiation::usr;

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

void Checker::walk(CXCursor root) {
    clang_visitChildren(root, &Checker::visit, this);
}

void Checker::walk_specialization(CXCursor specialization) {
    if (contract_probe(spelling(specialization))) {
        enter_probe(specialization);
        return;
    }
    walk(specialization);
}

std::vector<Diagnostic> Checker::take_findings() {
    return std::move(findings_);
}

CXChildVisitResult Checker::visit(CXCursor cursor, CXCursor, CXClientData data) {
    return static_cast<Checker*>(data)->enter(cursor);
}

void Checker::visit_subtree(CXCursor cursor) {
    if (enter(cursor) == CXChildVisit_Recurse) {
        clang_visitChildren(cursor, &Checker::visit, this);
    }
}

bool Checker::proof_only(std::size_t offset) const {
    const auto after = std::ranges::upper_bound(spans_, offset, {}, &source::ByteSpan::offset);
    return after != spans_.begin() && offset < std::prev(after)->end();
}

bool Checker::overlaps(std::size_t begin, std::size_t end) const {
    const auto first = std::ranges::upper_bound(spans_, begin, {}, &source::ByteSpan::end);
    return first != spans_.end() && first->offset <= end;
}

bool Checker::generated_name(std::string_view name) const {
    return !prefix_.empty() && name.starts_with(prefix_);
}

bool Checker::contract_probe(std::string_view name) const {
    if (!generated_name(name)) {
        return false;
    }
    const std::string_view rest = name.substr(prefix_.size());
    return rest.starts_with("ensures_") || rest.starts_with("expects_") || rest.starts_with("decreases_");
}

bool Checker::generated(CXCursor declaration) const {
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

bool Checker::scaffolding(CXCursorKind kind, std::string_view name, bool inside) const {
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
    return inside &&
           (kind == CXCursor_TypeAliasDecl || kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasTemplateDecl);
}

CXChildVisitResult Checker::enter(CXCursor cursor) {
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

void Checker::enter_probe(CXCursor probe) {
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

void Checker::check(CXCursor cursor, CXCursorKind kind) {
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

void Checker::check_call(CXCursor call, CXCursor referenced) {
    const std::string name = spelling(call);
    if (name.empty()) {
        return; // a dependent call's overload set is a cursor of its own
    }
    const bool member =
        !is_null(referenced) && is_function(kind_of(referenced)) && kind_of(referenced) != CXCursor_FunctionDecl;
    if (member) {
        const CXCursor owner = clang_getCursorSemanticParent(referenced);
        report(call, declaration_hazard(owner, clang_getCursorType(owner), 0));
        report(call, member_candidates(owner, [&name](std::string_view candidate) { return candidate == name; }, 0));
        if (!is_operator_name(name)) {
            return;
        }
    }
    report(call, namespace_candidates([&name](std::string_view candidate) { return candidate == name; }));
}

void Checker::check_operator(CXCursor expression, CXCursorKind kind) {
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

std::optional<Hazard> Checker::namespace_candidates(const std::function<bool(std::string_view)>& matches) {
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

std::optional<Hazard> Checker::member_candidates(CXCursor owner, const std::function<bool(std::string_view)>& matches,
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

void Checker::inventory() {
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

} // namespace cppl::clangbridge::detail
