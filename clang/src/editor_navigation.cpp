// Where a name leads: declarations, definitions, types and implementations,
// and every occurrence of a name.

#include "cppl/clang/editor.hpp"
#include "editor_state.hpp"

#include <algorithm>
#include <clang-c/CXFile.h>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace cppl::clangbridge {

using detail::editor::pointee_of;
using detail::editor::same;

namespace detail::editor {

// Types a declaration's own type leads to: through pointers, references and
// arrays to what they refer to, and through sugar to what it names.
CXType pointee_of(CXType type) {
    for (int depth = 0; depth < 32; ++depth) {
        switch (type.kind) {
            case CXType_Pointer:
            case CXType_LValueReference:
            case CXType_RValueReference:
            case CXType_BlockPointer:
            case CXType_MemberPointer:
                type = clang_getPointeeType(type);
                continue;
            case CXType_ConstantArray:
            case CXType_IncompleteArray:
            case CXType_VariableArray:
            case CXType_DependentSizedArray:
                type = clang_getArrayElementType(type);
                continue;
            case CXType_Elaborated:
                type = clang_Type_getNamedType(type);
                continue;
            case CXType_Auto:
            case CXType_Unexposed: {
                const CXType canonical = clang_getCanonicalType(type);
                if (canonical.kind == type.kind) {
                    return type;
                }
                type = canonical;
                continue;
            }
            default:
                return type;
        }
    }
    return type;
}

} // namespace detail::editor

namespace {

struct ImplementationSearch {
    std::string usr;
    bool method = false;
    std::vector<CXCursor> found;
};

bool overrides(CXCursor method, const std::string& usr, int depth = 0) {
    if (depth > 16) {
        return false;
    }
    CXCursor* overridden = nullptr;
    unsigned count = 0;
    clang_getOverriddenCursors(method, &overridden, &count);
    bool result = false;
    for (unsigned index = 0; index < count && !result; ++index) {
        result = take(clang_getCursorUSR(overridden[index])) == usr || overrides(overridden[index], usr, depth + 1);
    }
    clang_disposeOverriddenCursors(overridden);
    return result;
}

// Whether `record` derives from the class `usr` names, directly or through
// another base.
bool derives(CXCursor record, const std::string& usr, int depth = 0);

CXChildVisitResult find_base(CXCursor child, CXCursor, CXClientData data) {
    auto& [usr, depth, found] = *static_cast<std::tuple<const std::string*, int, bool>*>(data);
    if (clang_getCursorKind(child) != CXCursor_CXXBaseSpecifier) {
        return CXChildVisit_Continue;
    }
    const CXCursor base = clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(child)));
    if (is_null(base)) {
        return CXChildVisit_Continue;
    }
    const CXCursor definition = clang_getCursorDefinition(base);
    if (take(clang_getCursorUSR(base)) == *usr || (!is_null(definition) && derives(definition, *usr, depth + 1))) {
        found = true;
        return CXChildVisit_Break;
    }
    return CXChildVisit_Continue;
}

bool derives(CXCursor record, const std::string& usr, int depth) {
    if (depth > 16) {
        return false;
    }
    std::tuple<const std::string*, int, bool> search{&usr, depth, false};
    clang_visitChildren(record, find_base, &search);
    return std::get<2>(search);
}

CXChildVisitResult find_implementations(CXCursor cursor, CXCursor, CXClientData data) {
    auto& search = *static_cast<ImplementationSearch*>(data);
    if (clang_Location_isInSystemHeader(clang_getCursorLocation(cursor)) != 0) {
        return CXChildVisit_Continue;
    }
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (search.method) {
        if (kind == CXCursor_CXXMethod && overrides(cursor, search.usr)) {
            search.found.push_back(cursor);
        }
    } else if ((kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl || kind == CXCursor_ClassTemplate) &&
               clang_isCursorDefinition(cursor) != 0 && derives(cursor, search.usr)) {
        search.found.push_back(cursor);
    }
    return CXChildVisit_Recurse;
}

} // namespace

std::vector<Extent> EditorUnit::navigate(Destination destination, std::size_t offset) const {
    const State& state = *state_;
    std::vector<Extent> results;
    if (state.unit == nullptr || offset > state.main.text.size()) {
        return results;
    }

    const auto token = state.token_at(offset);
    if (!token.has_value()) {
        return results;
    }
    const CXCursor cursor = state.cursor_at(offset);
    if (is_null(cursor)) {
        return results;
    }

    // An `#include` leads to the file it includes, wherever on its line the
    // request is made.
    if (clang_getCursorKind(cursor) == CXCursor_InclusionDirective) {
        if (destination != Destination::Definition && destination != Destination::Declaration) {
            return results;
        }
        CXFile included = clang_getIncludedFile(cursor);
        if (included == nullptr) {
            return results;
        }
        FilePosition start;
        start.file = take(clang_getFileName(included));
        start.line = 1;
        start.column = 1;
        results.push_back(Extent{start, start});
        return results;
    }

    const bool deduced = token->first == CXToken_Keyword && (token->second == "auto" || token->second == "decltype");
    std::vector<CXCursor> targets;
    if (deduced) {
        // `auto` names the type it was deduced as.
        const CXCursor declared = clang_getTypeDeclaration(pointee_of(clang_getCursorType(cursor)));
        if (!is_null(declared)) {
            targets.push_back(declared);
        }
        destination = destination == Destination::Implementation ? destination : Destination::Definition;
    } else {
        targets = state.named_at(offset);
    }

    const auto add = [&](CXCursor target) {
        if (is_null(target)) {
            return;
        }
        if (std::optional<Extent> extent = state.name_extent(target)) {
            const bool repeated = std::ranges::any_of(results, [&](const Extent& known) {
                return known.begin.file == extent->begin.file && known.begin.offset == extent->begin.offset;
            });
            if (!repeated) {
                results.push_back(std::move(*extent));
            }
        }
    };

    for (const CXCursor target : targets) {
        switch (destination) {
            case Destination::Definition: {
                const CXCursor definition = clang_getCursorDefinition(target);
                if (is_null(definition)) {
                    add(target);
                    break;
                }
                // Asked at the definition itself, the answer is where it was
                // first declared, where that is somewhere else.
                if (same(definition, cursor)) {
                    const CXCursor canonical = clang_getCanonicalCursor(definition);
                    add(same(canonical, definition) ? definition : canonical);
                    break;
                }
                add(definition);
                break;
            }
            case Destination::Declaration:
                add(clang_getCanonicalCursor(target));
                break;
            case Destination::TypeDefinition: {
                CXType type = clang_getCursorType(target);
                const CXCursorKind kind = clang_getCursorKind(target);
                if (kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod || kind == CXCursor_FunctionTemplate) {
                    type = clang_getCursorResultType(target);
                }
                const CXCursor declared = clang_getTypeDeclaration(pointee_of(type));
                if (is_null(declared)) {
                    break;
                }
                const CXCursor definition = clang_getCursorDefinition(declared);
                add(is_null(definition) ? declared : definition);
                break;
            }
            case Destination::Implementation: {
                const CXCursorKind kind = clang_getCursorKind(target);
                ImplementationSearch search;
                search.usr = take(clang_getCursorUSR(target));
                if (search.usr.empty()) {
                    break;
                }
                if (kind == CXCursor_CXXMethod) {
                    if (clang_CXXMethod_isVirtual(target) == 0) {
                        break;
                    }
                    search.method = true;
                } else if (kind != CXCursor_ClassDecl && kind != CXCursor_StructDecl &&
                           kind != CXCursor_ClassTemplate) {
                    break;
                }
                clang_visitChildren(clang_getTranslationUnitCursor(state.unit), find_implementations, &search);
                for (const CXCursor found : search.found) {
                    add(found);
                }
                break;
            }
        }
    }
    return results;
}

std::vector<Entity> EditorUnit::entities_at(std::size_t offset) const {
    const State& state = *state_;
    std::vector<Entity> entities;
    if (state.unit == nullptr || offset > state.main.text.size()) {
        return entities;
    }
    for (const CXCursor target : state.named_at(offset)) {
        std::string usr = take(clang_getCursorUSR(target));
        if (usr.empty()) {
            continue;
        }
        entities.push_back(Entity{std::move(usr), take(clang_getCursorSpelling(target)),
                                  state.name_extent(clang_getCanonicalCursor(target))});
    }
    return entities;
}

std::vector<Occurrence> EditorUnit::all_occurrences() const {
    return state_->walk(nullptr);
}

std::vector<std::string> EditorUnit::renamed_together(const std::string& usr) const {
    return state_->renamed_together(usr);
}

std::vector<Occurrence> EditorUnit::unwritten_uses(const std::vector<std::string>* usrs) const {
    std::vector<Occurrence> unwritten;
    static_cast<void>(state_->walk(usrs, &unwritten));
    return unwritten;
}

} // namespace cppl::clangbridge
