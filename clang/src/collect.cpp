#include "collect.hpp"

#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "places.hpp"
#include "refinements.hpp"
#include "types.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// Which declarations of a unit are verified (SPEC.md 42, TEMPLATE-001,
// TEMPLATE-002): the functions the projector selected, the specializations of
// verified function templates the unit instantiates, and every place a
// refinement is written as a template argument or used at an unverified
// boundary (SPEC.md 17.6, STDMODEL-020).
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::take;

namespace {

bool is_selected(CXCursor cursor, const Selection& selection) {
    const std::string name = take(clang_getCursorSpelling(cursor));
    if (!selection.specification_prefix.empty() && name.starts_with(selection.specification_prefix)) {
        return true;
    }

    const auto offset = physical_offset(cursor);
    return std::ranges::find(selection.offsets, offset) != selection.offsets.end();
}

bool in_namespace_std(CXCursor cursor) {
    for (CXCursor scope = clang_getCursorSemanticParent(cursor);
         clang_Cursor_isNull(scope) == 0 && clang_getCursorKind(scope) != CXCursor_TranslationUnit;
         scope = clang_getCursorSemanticParent(scope)) {
        if (clang_getCursorKind(scope) == CXCursor_Namespace && take(clang_getCursorSpelling(scope)) == "std") {
            return true;
        }
    }
    return false;
}

} // namespace

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

std::string refined_template_argument_refusal(const RefinedTemplateArgument& refined) {
    return "refinement '" + refined.refinement + "' is written as a template argument of '" + refined.template_name +
           "', whose instantiation holds it as its base type, where nothing charges its predicate; a refinement is a "
           "template argument only where the sequence model states its elements (SPEC.md STDMODEL-020)";
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
    if (const auto refined = refined_template_argument(cursor, *collector.selection)) {
        collector.refused_arguments.emplace_back(refined_template_argument_refusal(*refined), cursor);
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
std::optional<std::string> refinement_use(CXCursor declaration, const Selection& selection, unsigned depth,
                                          std::unordered_set<std::size_t>* visited) {
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

// A refinement written as a template argument of a template outside the
// standard library, among the references `cursor` holds directly: a
// declaration whose type names such a template, or a reference to a
// specialization of such a function template. The instantiation holds the
// refinement as its base type, so nothing there charges the predicate while
// the refinement is still written (SPEC.md STDMODEL-020). A standard template
// with a refined argument is governed by the sequence model or refused by it.
std::optional<RefinedTemplateArgument> refined_template_argument(CXCursor cursor, const Selection& selection,
                                                                 unsigned depth) {
    if (selection.refinements.empty() || depth > kMaxExpressionDepth) {
        return std::nullopt;
    }
    std::optional<std::string> user_template;
    // A refinement a template's own parameter takes by default reaches every
    // use that leaves that argument out.
    const auto defaulted = [&](CXCursor named_template) -> std::optional<RefinedTemplateArgument> {
        for (const CXCursor parameter : children_of(named_template)) {
            if (clang_getCursorKind(parameter) != CXCursor_TemplateTypeParameter) {
                continue;
            }
            for (const CXCursor argument : children_of(parameter)) {
                if (clang_getCursorKind(argument) != CXCursor_TypeRef) {
                    continue;
                }
                if (auto refinement = refinement_use(clang_getCursorReferenced(argument), selection)) {
                    return RefinedTemplateArgument{std::move(*refinement), qualified_name_of(named_template)};
                }
            }
        }
        return std::nullopt;
    };
    const CXCursor referenced = clang_getCursorReferenced(cursor);
    if (clang_isExpression(clang_getCursorKind(cursor)) != 0 && clang_Cursor_isNull(referenced) == 0) {
        const CXCursor primary = clang_getSpecializedCursorTemplate(referenced);
        if (clang_Cursor_isNull(primary) == 0 && !in_namespace_std(primary)) {
            user_template = qualified_name_of(primary);
            if (auto by_default = defaulted(primary)) {
                return by_default;
            }
        }
    }
    const CXCursor initializer = clang_Cursor_getVarDeclInitializer(cursor);
    for (const CXCursor child : children_of(cursor)) {
        if (clang_Cursor_isNull(initializer) == 0 && clang_equalCursors(child, initializer) != 0) {
            break;
        }
        const CXCursorKind kind = clang_getCursorKind(child);
        if (kind == CXCursor_TemplateRef) {
            const CXCursor named = clang_getCursorReferenced(child);
            if (!user_template.has_value() && !in_namespace_std(named)) {
                user_template = qualified_name_of(named);
                if (auto by_default = defaulted(named)) {
                    return by_default;
                }
            }
            continue;
        }
        // `Box<decltype(p)>` names `p`'s type through `p`.
        if (kind == CXCursor_DeclRefExpr && user_template.has_value()) {
            if (auto refinement = refinement_use(clang_getCursorReferenced(child), selection)) {
                return RefinedTemplateArgument{std::move(*refinement), *user_template};
            }
            continue;
        }
        if (kind != CXCursor_TypeRef) {
            continue;
        }
        const CXCursor named = clang_getCursorReferenced(child);
        if (user_template.has_value()) {
            if (auto refinement = refinement_use(named, selection)) {
                return RefinedTemplateArgument{std::move(*refinement), *user_template};
            }
        }
        // An alias of such a specialization hides it from the declaration.
        const CXCursorKind named_kind = clang_getCursorKind(named);
        if (named_kind == CXCursor_TypeAliasDecl || named_kind == CXCursor_TypedefDecl) {
            if (auto hidden = refined_template_argument(named, selection, depth + 1)) {
                return hidden;
            }
        }
    }
    return std::nullopt;
}

} // namespace cppl::clangbridge::detail
