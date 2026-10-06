// How each name in the main file is classified, the inlay hints written
// beside names, and the files the unit includes.

#include "cppl/clang/editor.hpp"
#include "editor_state.hpp"

#include <algorithm>
#include <clang-c/CXFile.h>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cppl::clangbridge {

using detail::editor::in_main_file;

namespace {

std::optional<ClassifiedName::Kind> name_kind(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_Namespace:
        case CXCursor_NamespaceAlias:
            return ClassifiedName::Kind::Namespace;
        case CXCursor_ClassDecl:
        case CXCursor_ClassTemplate:
        case CXCursor_ClassTemplatePartialSpecialization:
        case CXCursor_Constructor: // a constructor's and a destructor's name spells the class
        case CXCursor_Destructor:
            return ClassifiedName::Kind::Class;
        case CXCursor_StructDecl:
        case CXCursor_UnionDecl:
            return ClassifiedName::Kind::Struct;
        case CXCursor_EnumDecl:
            return ClassifiedName::Kind::Enum;
        case CXCursor_TypedefDecl:
        case CXCursor_TypeAliasDecl:
        case CXCursor_TypeAliasTemplateDecl:
        case CXCursor_ConceptDecl:
            return ClassifiedName::Kind::Type;
        case CXCursor_TemplateTypeParameter:
        case CXCursor_NonTypeTemplateParameter:
        case CXCursor_TemplateTemplateParameter:
            return ClassifiedName::Kind::TypeParameter;
        case CXCursor_ParmDecl:
            return ClassifiedName::Kind::Parameter;
        case CXCursor_VarDecl:
            return ClassifiedName::Kind::Variable;
        case CXCursor_FieldDecl:
            return ClassifiedName::Kind::Field;
        case CXCursor_EnumConstantDecl:
            return ClassifiedName::Kind::Enumerator;
        case CXCursor_FunctionDecl:
        case CXCursor_FunctionTemplate:
            return ClassifiedName::Kind::Function;
        case CXCursor_CXXMethod:
        case CXCursor_ConversionFunction:
            return ClassifiedName::Kind::Method;
        case CXCursor_MacroDefinition:
        case CXCursor_MacroExpansion:
            return ClassifiedName::Kind::Macro;
        default:
            return std::nullopt;
    }
}

// Whether a declaration's value cannot change: a `const` object, or an
// enumerator.
bool constant(CXCursor declaration) {
    if (clang_getCursorKind(declaration) == CXCursor_EnumConstantDecl) {
        return true;
    }
    CXType type = clang_getCursorType(declaration);
    if (type.kind == CXType_LValueReference || type.kind == CXType_RValueReference) {
        type = clang_getPointeeType(type);
    }
    return clang_isConstQualifiedType(type) != 0;
}

} // namespace

std::vector<ClassifiedName> EditorUnit::classified_names() const {
    std::vector<ClassifiedName> found;
    const State& state = *state_;
    CXFile file = state.unit != nullptr ? state.main_file() : nullptr;
    if (file == nullptr) {
        return found;
    }
    const State::Tokens tokens(
        state.unit,
        clang_getRange(clang_getLocationForOffset(state.unit, file, 0),
                       clang_getLocationForOffset(state.unit, file, static_cast<unsigned>(state.main.text.size()))));
    std::vector<CXCursor> cursors(tokens.size());
    clang_annotateTokens(state.unit, tokens.data(), tokens.size(), cursors.data());
    for (unsigned position = 0; position < tokens.size(); ++position) {
        const CXToken token = tokens[position];
        if (clang_getTokenKind(token) != CXToken_Identifier) {
            continue;
        }
        const CXCursor cursor = cursors[position];
        const CXCursorKind kind = clang_getCursorKind(cursor);
        const bool at_cursor =
            clang_equalLocations(clang_getCursorLocation(cursor), clang_getTokenLocation(state.unit, token)) != 0;
        // A macro's definition and each use of it cover every token they
        // span; only the macro's own name names the macro.
        if ((kind == CXCursor_MacroDefinition || kind == CXCursor_MacroExpansion) && !at_cursor) {
            continue;
        }
        const bool declares = clang_isDeclaration(kind) != 0 || kind == CXCursor_MacroDefinition;
        // What the name stands for: the declaration it writes, or the one it
        // refers to.
        const CXCursor named = declares || kind == CXCursor_MacroExpansion ? cursor : clang_getCursorReferenced(cursor);
        if (is_null(named)) {
            continue;
        }
        const std::optional<ClassifiedName::Kind> classified = name_kind(clang_getCursorKind(named));
        if (!classified.has_value()) {
            continue;
        }
        const CXSourceRange extent = clang_getTokenExtent(state.unit, token);
        ClassifiedName name;
        name.kind = *classified;
        name.name = Extent{state.place(clang_getRangeStart(extent)), state.place(clang_getRangeEnd(extent))};
        if (!name.name.begin.in_main_file) {
            continue;
        }
        // A declaration's cursor covers its type and its initializer too; its
        // name is written only where the cursor's location is.
        name.declaration = declares && at_cursor;
        name.constant = constant(named);
        // A static member, which belongs to its class rather than to an object.
        const CXCursorKind owner = clang_getCursorKind(clang_getCursorSemanticParent(named));
        const bool member_of_class = owner == CXCursor_ClassDecl || owner == CXCursor_StructDecl ||
                                     owner == CXCursor_UnionDecl || owner == CXCursor_ClassTemplate ||
                                     owner == CXCursor_ClassTemplatePartialSpecialization;
        name.is_static = clang_CXXMethod_isStatic(named) != 0 ||
                         (member_of_class && clang_Cursor_getStorageClass(named) == CX_SC_Static);
        name.deprecated = clang_getCursorAvailability(named) == CXAvailability_Deprecated;
        name.library = clang_Location_isInSystemHeader(clang_getCursorLocation(named)) != 0;
        found.push_back(std::move(name));
    }
    return found;
}

std::vector<Hint> EditorUnit::hints() const {
    std::vector<Hint> found;
    if (state_->unit == nullptr) {
        return found;
    }
    struct Walk {
        const State* state;
        std::vector<Hint>* found;
    } walk{state_.get(), &found};
    clang_visitChildren(
        clang_getTranslationUnitCursor(state_->unit),
        [](CXCursor cursor, CXCursor parent, CXClientData data) {
            const Walk& walk = *static_cast<Walk*>(data);
            if (!in_main_file(cursor, parent)) {
                return CXChildVisit_Continue;
            }
            const CXCursorKind kind = clang_getCursorKind(cursor);
            if (kind == CXCursor_CallExpr) {
                walk.state->parameter_hints(cursor, *walk.found);
            } else if (kind == CXCursor_VarDecl) {
                walk.state->type_hint(cursor, *walk.found);
            }
            return CXChildVisit_Recurse;
        },
        &walk);
    return found;
}

std::vector<std::string> EditorUnit::included_files() const {
    std::vector<std::string> files;
    if (state_->unit == nullptr) {
        return files;
    }
    struct Visit {
        CXTranslationUnit unit;
        std::vector<std::string>* files;
    } visit{state_->unit, &files};
    clang_getInclusions(
        state_->unit,
        [](CXFile included, CXSourceLocation*, unsigned depth, CXClientData data) {
            auto& [unit, found] = *static_cast<Visit*>(data);
            if (depth == 0) {
                return; // the main file itself
            }
            if (clang_Location_isInSystemHeader(clang_getLocation(unit, included, 1, 1)) != 0) {
                return;
            }
            std::string name = take(clang_getFileName(included));
            if (std::ranges::find(*found, name) == found->end()) {
                found->push_back(std::move(name));
            }
        },
        &visit);
    return files;
}

} // namespace cppl::clangbridge
