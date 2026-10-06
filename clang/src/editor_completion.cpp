// Completion, signature help, and the scope a position is in.

#include "cppl/clang/editor.hpp"
#include "editor_state.hpp"

#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cppl::clangbridge {

namespace {

Completion::Kind completion_kind(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_FunctionDecl:
        case CXCursor_FunctionTemplate:
            return Completion::Kind::Function;
        case CXCursor_CXXMethod:
        case CXCursor_Destructor:
        case CXCursor_ConversionFunction:
            return Completion::Kind::Method;
        case CXCursor_Constructor:
            return Completion::Kind::Constructor;
        case CXCursor_FieldDecl:
            return Completion::Kind::Field;
        case CXCursor_VarDecl:
            return Completion::Kind::Variable;
        case CXCursor_ParmDecl:
            return Completion::Kind::Parameter;
        case CXCursor_ClassDecl:
        case CXCursor_ClassTemplate:
        case CXCursor_ClassTemplatePartialSpecialization:
            return Completion::Kind::Class;
        case CXCursor_StructDecl:
        case CXCursor_UnionDecl:
            return Completion::Kind::Struct;
        case CXCursor_EnumDecl:
            return Completion::Kind::Enum;
        case CXCursor_EnumConstantDecl:
            return Completion::Kind::Enumerator;
        case CXCursor_Namespace:
        case CXCursor_NamespaceAlias:
            return Completion::Kind::Namespace;
        case CXCursor_TypedefDecl:
        case CXCursor_TypeAliasDecl:
        case CXCursor_TypeAliasTemplateDecl:
            return Completion::Kind::TypeAlias;
        case CXCursor_TemplateTypeParameter:
        case CXCursor_NonTypeTemplateParameter:
        case CXCursor_TemplateTemplateParameter:
            return Completion::Kind::TemplateParameter;
        case CXCursor_MacroDefinition:
            return Completion::Kind::Macro;
        case CXCursor_ConceptDecl:
            return Completion::Kind::Concept;
        case CXCursor_NotImplemented:
            return Completion::Kind::Keyword;
        default:
            return Completion::Kind::Other;
    }
}

// Text a snippet shows as written: `$`, `}` and `\` would otherwise start or
// end a tab stop.
std::string snippet_escaped(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        if (character == '$' || character == '}' || character == '\\') {
            out += '\\';
        }
        out += character;
    }
    return out;
}

void render(CXCompletionString string, Completion& completion, int& placeholders) {
    const unsigned count = clang_getNumCompletionChunks(string);
    for (unsigned index = 0; index < count; ++index) {
        const CXCompletionChunkKind kind = clang_getCompletionChunkKind(string, index);
        if (kind == CXCompletionChunk_Optional) {
            // Defaulted parameters are left out, as a call usually leaves them.
            continue;
        }
        const std::string text = take(clang_getCompletionChunkText(string, index));
        switch (kind) {
            case CXCompletionChunk_TypedText:
                completion.typed += text;
                completion.label += text;
                completion.snippet += snippet_escaped(text);
                break;
            case CXCompletionChunk_ResultType:
                completion.result = text;
                break;
            case CXCompletionChunk_Placeholder:
            case CXCompletionChunk_CurrentParameter:
                completion.label += text;
                completion.snippet += "${" + std::to_string(++placeholders) + ":" + snippet_escaped(text) + "}";
                break;
            case CXCompletionChunk_Informative:
                completion.label += text;
                break;
            case CXCompletionChunk_VerticalSpace:
                completion.label += ' ';
                completion.snippet += '\n';
                break;
            default:
                completion.label += text;
                completion.snippet += snippet_escaped(text);
                break;
        }
    }
}

// The 1-based line and column of a byte offset in `text`.
std::pair<unsigned, unsigned> line_and_column(const std::string& text, std::size_t offset) {
    unsigned line = 1;
    std::size_t line_start = 0;
    for (std::size_t index = 0; index < offset && index < text.size(); ++index) {
        if (text[index] == '\n') {
            ++line;
            line_start = index + 1;
        }
    }
    return {line, static_cast<unsigned>(offset - line_start + 1)};
}

} // namespace

std::vector<Completion> EditorUnit::complete(std::size_t offset) const {
    const State& state = *state_;
    std::vector<Completion> completions;
    if (state.unit == nullptr || offset > state.main.text.size()) {
        return completions;
    }
    const auto [line, column] = line_and_column(state.main.text, offset);
    const std::vector<FileContent> all = state.files();
    std::vector<CXUnsavedFile> view = unsaved_view(all);
    CXCodeCompleteResults* results = clang_codeCompleteAt(
        state.unit, state.main.path.c_str(), line, column, view.data(), static_cast<unsigned>(view.size()),
        clang_defaultCodeCompleteOptions() | CXCodeComplete_IncludeBriefComments);
    if (results == nullptr) {
        return completions;
    }
    completions.reserve(results->NumResults);
    for (unsigned index = 0; index < results->NumResults; ++index) {
        const CXCompletionResult& result = results->Results[index];
        if (result.CursorKind == CXCursor_OverloadCandidate) {
            continue;
        }
        const CXAvailabilityKind availability = clang_getCompletionAvailability(result.CompletionString);
        if (availability == CXAvailability_NotAvailable || availability == CXAvailability_NotAccessible) {
            continue;
        }
        Completion completion;
        completion.kind = completion_kind(result.CursorKind);
        int placeholders = 0;
        render(result.CompletionString, completion, placeholders);
        if (completion.typed.empty()) {
            continue;
        }
        completion.documentation = take(clang_getCompletionBriefComment(result.CompletionString));
        completion.priority = clang_getCompletionPriority(result.CompletionString);
        completion.deprecated = availability == CXAvailability_Deprecated;
        completions.push_back(std::move(completion));
    }
    clang_disposeCodeCompleteResults(results);
    return completions;
}

std::vector<Signature> EditorUnit::signatures(std::size_t offset) const {
    const State& state = *state_;
    std::vector<Signature> found;
    if (state.unit == nullptr || offset > state.main.text.size()) {
        return found;
    }
    const auto [line, column] = line_and_column(state.main.text, offset);
    const std::vector<FileContent> all = state.files();
    std::vector<CXUnsavedFile> view = unsaved_view(all);
    CXCodeCompleteResults* results =
        clang_codeCompleteAt(state.unit, state.main.path.c_str(), line, column, view.data(),
                             static_cast<unsigned>(view.size()), CXCodeComplete_IncludeBriefComments);
    if (results == nullptr) {
        return found;
    }
    for (unsigned index = 0; index < results->NumResults; ++index) {
        const CXCompletionResult& result = results->Results[index];
        if (result.CursorKind != CXCursor_OverloadCandidate) {
            continue;
        }
        Signature signature;
        std::string result_type;
        const unsigned count = clang_getNumCompletionChunks(result.CompletionString);
        for (unsigned chunk = 0; chunk < count; ++chunk) {
            const CXCompletionChunkKind kind = clang_getCompletionChunkKind(result.CompletionString, chunk);
            if (kind == CXCompletionChunk_Optional) {
                continue;
            }
            const std::string text = take(clang_getCompletionChunkText(result.CompletionString, chunk));
            if (kind == CXCompletionChunk_ResultType) {
                result_type = text;
                continue;
            }
            if (kind == CXCompletionChunk_Placeholder || kind == CXCompletionChunk_CurrentParameter) {
                if (kind == CXCompletionChunk_CurrentParameter) {
                    signature.active = static_cast<std::uint32_t>(signature.parameters.size());
                }
                const auto start = static_cast<std::uint32_t>(signature.label.size());
                signature.label += text;
                signature.parameters.emplace_back(start, static_cast<std::uint32_t>(signature.label.size()));
                continue;
            }
            signature.label += text;
        }
        if (!result_type.empty()) {
            // The result type leads, as a declaration spells it; every
            // parameter range moves with it.
            const auto shift = static_cast<std::uint32_t>(result_type.size() + 1);
            signature.label = result_type + " " + signature.label;
            for (auto& [start, end] : signature.parameters) {
                start += shift;
                end += shift;
            }
        }
        signature.documentation = take(clang_getCompletionBriefComment(result.CompletionString));
        found.push_back(std::move(signature));
    }
    clang_disposeCodeCompleteResults(results);
    return found;
}

Scope EditorUnit::scope_at(std::size_t offset) const {
    const State& state = *state_;
    if (state.unit == nullptr) {
        return Scope::Other;
    }
    CXCursor cursor = state.cursor_at(offset);
    // Between declarations no entity is there, and what encloses the position
    // is the translation unit.
    if (is_null(cursor)) {
        return Scope::Namespace;
    }
    for (; !is_null(cursor); cursor = clang_getCursorSemanticParent(cursor)) {
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_TranslationUnit || kind == CXCursor_Namespace || kind == CXCursor_LinkageSpec) {
            return Scope::Namespace;
        }
        if (kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl ||
            kind == CXCursor_ClassTemplate) {
            return Scope::Class;
        }
        if (clang_isStatement(kind) != 0 || clang_isExpression(kind) != 0 || kind == CXCursor_FunctionDecl ||
            kind == CXCursor_CXXMethod || kind == CXCursor_Constructor || kind == CXCursor_Destructor ||
            kind == CXCursor_FunctionTemplate) {
            return Scope::Function;
        }
    }
    return Scope::Other;
}

} // namespace cppl::clangbridge
