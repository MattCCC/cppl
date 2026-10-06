// The outline of the main file, its folding ranges, and the declaration
// enclosing a position.

#include "cppl/clang/editor.hpp"
#include "editor_state.hpp"

#include <algorithm>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cppl::clangbridge {

using detail::editor::in_main_file;
using detail::editor::same;

namespace {

std::optional<Symbol::Kind> symbol_kind(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_Namespace:
            return Symbol::Kind::Namespace;
        case CXCursor_ClassDecl:
        case CXCursor_ClassTemplate:
        case CXCursor_ClassTemplatePartialSpecialization:
            return Symbol::Kind::Class;
        case CXCursor_StructDecl:
            return Symbol::Kind::Struct;
        case CXCursor_UnionDecl:
            return Symbol::Kind::Union;
        case CXCursor_EnumDecl:
            return Symbol::Kind::Enum;
        case CXCursor_EnumConstantDecl:
            return Symbol::Kind::Enumerator;
        case CXCursor_FunctionDecl:
        case CXCursor_FunctionTemplate:
            return Symbol::Kind::Function;
        case CXCursor_CXXMethod:
        case CXCursor_Destructor:
        case CXCursor_ConversionFunction:
            return Symbol::Kind::Method;
        case CXCursor_Constructor:
            return Symbol::Kind::Constructor;
        case CXCursor_FieldDecl:
            return Symbol::Kind::Field;
        case CXCursor_VarDecl:
            return Symbol::Kind::Variable;
        case CXCursor_TypedefDecl:
        case CXCursor_TypeAliasDecl:
        case CXCursor_TypeAliasTemplateDecl:
            return Symbol::Kind::TypeAlias;
        case CXCursor_MacroDefinition:
            return Symbol::Kind::Macro;
        case CXCursor_ConceptDecl:
            return Symbol::Kind::Concept;
        default:
            return std::nullopt;
    }
}

bool nests(Symbol::Kind kind) {
    return kind == Symbol::Kind::Namespace || kind == Symbol::Kind::Class || kind == Symbol::Kind::Struct ||
           kind == Symbol::Kind::Union || kind == Symbol::Kind::Enum;
}

} // namespace

CXChildVisitResult EditorUnit::State::outline_visit(CXCursor cursor, CXCursor, CXClientData data) {
    const OutlineWalk& walk = *static_cast<OutlineWalk*>(data);
    const State& state = *walk.state;
    // Every header's declarations are the unit's too; only the main file's are
    // its outline.
    if (clang_Location_isFromMainFile(clang_getCursorLocation(cursor)) == 0) {
        return CXChildVisit_Continue;
    }
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_LinkageSpec) {
        return CXChildVisit_Recurse; // `extern "C" { ... }` declares into its enclosing scope
    }
    const std::optional<Symbol::Kind> symbol_kind_of = symbol_kind(kind);
    if (!symbol_kind_of.has_value() || (kind == CXCursor_MacroDefinition && clang_Cursor_isMacroBuiltin(cursor) != 0)) {
        return CXChildVisit_Continue;
    }
    const CXSourceRange range = clang_getCursorExtent(cursor);
    const Extent extent{state.place(clang_getRangeStart(range)), state.place(clang_getRangeEnd(range))};
    // Clang spells an unnamed namespace or type as where it is written; it has
    // no name, so it is selected at its first byte.
    const bool anonymous = clang_Cursor_isAnonymous(cursor) != 0;
    const std::optional<Extent> name = anonymous ? Extent{extent.begin, extent.begin} : state.name_extent(cursor);
    if (!name.has_value() || !name->begin.in_main_file) {
        return CXChildVisit_Continue;
    }
    Symbol symbol;
    symbol.kind = *symbol_kind_of;
    symbol.name = anonymous ? std::string() : take(clang_getCursorSpelling(cursor));
    if (symbol.name.empty()) {
        symbol.name = "(anonymous)";
    }
    // A member defined outside its class is named with its class.
    const CXCursor semantic = clang_getCursorSemanticParent(cursor);
    const CXCursor lexical = clang_getCursorLexicalParent(cursor);
    if (!is_null(semantic) && !same(semantic, lexical) && symbol.kind != Symbol::Kind::Namespace &&
        clang_getCursorKind(semantic) != CXCursor_TranslationUnit) {
        const std::string owner = take(clang_getCursorSpelling(semantic));
        if (!owner.empty()) {
            symbol.name = owner + "::" + symbol.name;
        }
    }
    if (kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl) {
        symbol.detail = take(clang_getTypeSpelling(clang_getTypedefDeclUnderlyingType(cursor)));
    } else if (kind != CXCursor_MacroDefinition && !nests(symbol.kind)) {
        symbol.detail = take(clang_getTypeSpelling(clang_getCursorType(cursor)));
    }
    symbol.name_extent = *name;
    symbol.extent = extent;
    if (nests(symbol.kind)) {
        OutlineWalk inner{&state, &symbol.children};
        clang_visitChildren(cursor, outline_visit, &inner);
    }
    walk.into->push_back(std::move(symbol));
    return CXChildVisit_Continue;
}

std::vector<Symbol> EditorUnit::outline() const {
    std::vector<Symbol> symbols;
    if (state_->unit != nullptr) {
        State::OutlineWalk walk{state_.get(), &symbols};
        clang_visitChildren(clang_getTranslationUnitCursor(state_->unit), State::outline_visit, &walk);
    }
    return symbols;
}

std::vector<Fold> EditorUnit::folds() const {
    std::vector<Fold> found;
    if (state_->unit == nullptr) {
        return found;
    }
    struct Walk {
        const State* state;
        std::vector<Fold>* found;
    } walk{state_.get(), &found};
    clang_visitChildren(
        clang_getTranslationUnitCursor(state_->unit),
        [](CXCursor cursor, CXCursor parent, CXClientData data) {
            const Walk& walk = *static_cast<Walk*>(data);
            if (!in_main_file(cursor, parent)) {
                return CXChildVisit_Continue;
            }
            std::optional<Extent> body;
            switch (clang_getCursorKind(cursor)) {
                case CXCursor_InclusionDirective: {
                    const CXSourceRange range = clang_getCursorExtent(cursor);
                    walk.found->push_back(
                        Fold{Fold::Kind::Include, Extent{walk.state->place(clang_getRangeStart(range)),
                                                         walk.state->place(clang_getRangeEnd(range))}});
                    return CXChildVisit_Continue;
                }
                case CXCursor_CompoundStmt:
                case CXCursor_InitListExpr:
                    body = walk.state->braced(cursor);
                    break;
                case CXCursor_Namespace:
                case CXCursor_LinkageSpec:
                case CXCursor_ClassDecl:
                case CXCursor_StructDecl:
                case CXCursor_UnionDecl:
                case CXCursor_EnumDecl:
                case CXCursor_ClassTemplate:
                case CXCursor_ClassTemplatePartialSpecialization:
                    body = walk.state->declared_body(cursor);
                    break;
                default:
                    break;
            }
            if (body.has_value() && body->begin.in_main_file && body->end.in_main_file) {
                walk.found->push_back(Fold{Fold::Kind::Braces, *body});
            }
            return CXChildVisit_Recurse;
        },
        &walk);
    state_->conditional_branches(found);
    return found;
}

std::vector<Extent> EditorUnit::enclosing(std::size_t offset) const {
    std::vector<Extent> found;
    if (state_->unit == nullptr) {
        return found;
    }
    struct Walk {
        const State* state;
        std::size_t offset;
        std::vector<Extent>* found;
    } walk{state_.get(), offset, &found};
    clang_visitChildren(
        clang_getTranslationUnitCursor(state_->unit),
        [](CXCursor cursor, CXCursor parent, CXClientData data) {
            const Walk& walk = *static_cast<Walk*>(data);
            if (!in_main_file(cursor, parent)) {
                return CXChildVisit_Continue;
            }
            const CXSourceRange range = clang_getCursorExtent(cursor);
            const Extent extent{walk.state->place(clang_getRangeStart(range)),
                                walk.state->place(clang_getRangeEnd(range))};
            if (!extent.begin.in_main_file || !extent.end.in_main_file || extent.begin.offset > walk.offset ||
                walk.offset > extent.end.offset) {
                return CXChildVisit_Continue;
            }
            walk.found->push_back(extent);
            return CXChildVisit_Recurse;
        },
        &walk);
    std::ranges::reverse(found);
    return found;
}

} // namespace cppl::clangbridge
