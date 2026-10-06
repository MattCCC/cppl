#include "refinements.hpp"

#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "places.hpp"
#include "types.hpp"

#include <algorithm>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

// The refinements a declaration's written type names, recovered through the
// aliases Clang canonicalizes away (SPEC.md 17, 18), and the element type a
// modeled sequence is written with.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::take;

namespace {

std::vector<std::int64_t> refinement_arguments(CXCursor declared) {
    std::vector<std::int64_t> arguments;
    const CXCursor initializer = clang_Cursor_getVarDeclInitializer(declared);
    // In a member, variable or parameter, index arguments are written in the
    // type, before the declared name; a member's default initializer, like a
    // variable's or a parameter's default, comes after it. (An alias declares
    // its name before its type.)
    unsigned name_offset = 0;
    const CXCursorKind declared_kind = clang_getCursorKind(declared);
    if (declared_kind == CXCursor_FieldDecl || declared_kind == CXCursor_VarDecl ||
        declared_kind == CXCursor_ParmDecl) {
        clang_getFileLocation(clang_getCursorLocation(declared), nullptr, nullptr, nullptr, &name_offset);
    }
    for (const CXCursor child : children_of(declared)) {
        if (!clang_Cursor_isNull(initializer) && clang_equalCursors(child, initializer))
            break;
        unsigned child_offset = 0;
        clang_getFileLocation(clang_getCursorLocation(child), nullptr, nullptr, nullptr, &child_offset);
        if (name_offset != 0 && child_offset > name_offset)
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

} // namespace

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
std::expected<std::vector<Refinement>, RefinementFailure> refinements_of(
    CXCursor declared, CXType written, const std::vector<Selection::Refinement>& known) {
    const auto fail = [](Category category, std::string message) {
        return std::unexpected(RefinementFailure{category, std::move(message)});
    };
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
                return fail(Category::Elaboration, "refinement alias template has no resolved alias declaration");
            declaration = *alias;
        }
        const CXCursorKind kind = clang_getCursorKind(declaration);
        if (kind != CXCursor_TypeAliasDecl && kind != CXCursor_TypedefDecl && kind != CXCursor_TypeAliasTemplateDecl) {
            // A spelling this cannot follow to the declaration it names --
            // `decltype(...)`, or the member an alias template like
            // `std::type_identity_t` reaches -- may stand for a refinement,
            // and reading it as its base type would drop the predicate it
            // names (SPEC.md STDMODEL-020, FORALL-001). A substituted template
            // parameter spells its canonical type and is followed as it.
            const bool unnamed = clang_Cursor_isNull(declaration) != 0 || kind == CXCursor_NoDeclFound ||
                                 kind == CXCursor_TemplateTypeParameter;
            if (written.kind == CXType_Unexposed && unnamed &&
                take(clang_getTypeSpelling(written)) != take(clang_getTypeSpelling(clang_getCanonicalType(written)))) {
                return fail(Category::UnsupportedSemantics,
                            "a type written as '" + take(clang_getTypeSpelling(written)) +
                                "', which may name a refinement through a spelling this implementation does not "
                                "follow; write the refinement or its base type directly");
            }
            return found;
        }
        if (std::ranges::any_of(visited, [&](CXCursor previous) { return clang_equalCursors(previous, declaration); }))
            return fail(Category::Internal, "cyclic refinement alias metadata"); // Clang accepts no alias cycle
        visited.push_back(declaration);
        // The projector records the generated alias's physical identity. Source
        // spelling and presumed #line locations cannot identify a refinement.
        const auto entry = std::ranges::find(known, physical_offset(declaration), &Selection::Refinement::alias_offset);
        if (entry != known.end()) {
            if (entry->index_count != arguments.size())
                return fail(Category::Elaboration, "refinement '" + entry->name + "' has unresolved index arguments");
            found.push_back(Refinement{entry->name, arguments, entry->probe});
        }
        const CXType underlying = clang_getTypedefDeclUnderlyingType(declaration);
        if (underlying.kind == CXType_Invalid) {
            if (kind == CXCursor_TypeAliasTemplateDecl)
                return fail(Category::Elaboration,
                            "dependent refinement alias substitution is not resolved by the Clang bridge");
            return found;
        }
        written = underlying;
        // An ordinary alias may name an indexed refinement. Read that alias's
        // resolved application, not the initializer or a previous alias's indices.
        arguments = refinement_arguments(declaration);
    }
    return fail(Category::UnsupportedSemantics, "refinement alias chain exceeds the analysis limit");
}

// The element type of a modeled sequence as `written` spells it, with the
// refinements that spelling names (SPEC.md 17.6, RFC 0020 §6).
//
// Clang canonicalizes `std::vector<Positive>` to `std::vector<int>`, which is
// the runtime type and loses what verification needs, so the written type is
// followed through its aliases to the specialization as written, whose first
// argument keeps the alias a refinement is named by. A spelling this cannot
// follow is refused rather than read as an unrefined element.
// The first template argument of a specialization as `written` spells it, past
// references, elaboration and aliases, or an invalid type when the spelling
// cannot be followed.
CXType written_element_type(CXType written) {
    for (unsigned step = 0; step < kMaxExpressionDepth; ++step) {
        if (written.kind == CXType_LValueReference || written.kind == CXType_RValueReference) {
            written = clang_getPointeeType(written);
            continue;
        }
        if (clang_Type_getNumTemplateArguments(written) >= 1) {
            return clang_Type_getTemplateArgumentAsType(written, 0);
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
    return CXType{CXType_Invalid, {nullptr, nullptr}};
}

std::expected<Type, std::string> sequence_element(CXCursor declared, CXType written,
                                                  const std::vector<Selection::Refinement>* known) {
    const CXType element = written_element_type(written);
    if (element.kind == CXType_Invalid) {
        return std::unexpected("its element type could not be read from how its type is written");
    }
    Type converted = convert_type(element);
    if (converted.kind != TypeKind::Int && converted.kind != TypeKind::Bool) {
        return std::unexpected("its element type '" + converted.spelling + "' is not modeled");
    }
    if (known != nullptr) {
        auto refinements = refinements_of(declared, element, *known);
        if (!refinements) {
            return std::unexpected("its element type has " + refinements.error().message);
        }
        converted.refinements = std::move(*refinements);
    }
    return converted;
}

std::size_t physical_offset(CXCursor cursor) {
    unsigned offset = 0;
    clang_getFileLocation(clang_getCursorLocation(cursor), nullptr, nullptr, nullptr, &offset);
    return static_cast<std::size_t>(offset);
}

} // namespace cppl::clangbridge::detail
