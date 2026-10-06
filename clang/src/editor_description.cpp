// What Clang knows about a name, described for hover.

#include "cppl/clang/editor.hpp"
#include "editor_state.hpp"

#include <algorithm>
#include <cctype>
#include <clang-c/CXFile.h>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::clangbridge {

using detail::editor::pointee_of;

namespace {

std::string kind_in_words(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_FunctionDecl:
            return "function";
        case CXCursor_FunctionTemplate:
            return "function template";
        case CXCursor_CXXMethod:
            return "method";
        case CXCursor_Constructor:
            return "constructor";
        case CXCursor_Destructor:
            return "destructor";
        case CXCursor_ConversionFunction:
            return "conversion";
        case CXCursor_VarDecl:
            return "variable";
        case CXCursor_ParmDecl:
            return "parameter";
        case CXCursor_FieldDecl:
            return "field";
        case CXCursor_StructDecl:
            return "struct";
        case CXCursor_ClassDecl:
            return "class";
        case CXCursor_UnionDecl:
            return "union";
        case CXCursor_ClassTemplate:
        case CXCursor_ClassTemplatePartialSpecialization:
            return "class template";
        case CXCursor_EnumDecl:
            return "enum";
        case CXCursor_EnumConstantDecl:
            return "enumerator";
        case CXCursor_Namespace:
            return "namespace";
        case CXCursor_NamespaceAlias:
            return "namespace alias";
        case CXCursor_TypedefDecl:
            return "typedef";
        case CXCursor_TypeAliasDecl:
            return "type alias";
        case CXCursor_TypeAliasTemplateDecl:
            return "alias template";
        case CXCursor_TemplateTypeParameter:
        case CXCursor_NonTypeTemplateParameter:
        case CXCursor_TemplateTemplateParameter:
            return "template parameter";
        case CXCursor_MacroDefinition:
            return "macro";
        case CXCursor_ConceptDecl:
            return "concept";
        case CXCursor_LabelStmt:
            return "label";
        default:
            return "declaration";
    }
}

// A comment as its author meant it read: without `//`, `///`, `/*`, `*/` or
// the `*` that starts each line of a block.
std::string comment_text(const std::string& raw) {
    std::string out;
    std::size_t start = 0;
    while (start <= raw.size()) {
        const std::size_t end = std::min(raw.find('\n', start), raw.size());
        std::string_view line(raw.data() + start, end - start);
        const auto trim = [](std::string_view text) {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
                text.remove_prefix(1);
            }
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
                text.remove_suffix(1);
            }
            return text;
        };
        line = trim(line);
        for (const std::string_view marker : {"///<", "//!<", "///", "//!", "//", "/**<", "/**", "/*!", "/*"}) {
            if (line.starts_with(marker)) {
                line.remove_prefix(marker.size());
                break;
            }
        }
        if (line.ends_with("*/")) {
            line.remove_suffix(2);
        }
        if (line.starts_with("*")) {
            line.remove_prefix(1);
        }
        line = trim(line);
        if (!line.empty() || (!out.empty() && !out.ends_with("\n\n"))) {
            out += line;
            out += '\n';
        }
        start = end + 1;
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

std::string qualified(CXCursor cursor) {
    std::vector<std::string> parts{take(clang_getCursorSpelling(cursor))};
    CXCursor parent = clang_getCursorSemanticParent(cursor);
    for (int depth = 0; depth < 64 && !is_null(parent); ++depth) {
        const CXCursorKind kind = clang_getCursorKind(parent);
        if (kind != CXCursor_Namespace && kind != CXCursor_ClassDecl && kind != CXCursor_StructDecl &&
            kind != CXCursor_UnionDecl && kind != CXCursor_EnumDecl && kind != CXCursor_ClassTemplate &&
            kind != CXCursor_ClassTemplatePartialSpecialization) {
            break;
        }
        // An enumerator of an unscoped enumeration is named without it.
        if (kind == CXCursor_EnumDecl && clang_EnumDecl_isScoped(parent) == 0) {
            parent = clang_getCursorSemanticParent(parent);
            continue;
        }
        const std::string name = take(clang_getCursorSpelling(parent));
        parts.push_back(name.empty() ? "(anonymous)" : name);
        parent = clang_getCursorSemanticParent(parent);
    }
    std::string out;
    for (const std::string& part : std::views::reverse(parts)) {
        out += (out.empty() ? "" : "::") + part;
    }
    return out;
}

std::string pretty(CXCursor cursor) {
    CXPrintingPolicy policy = clang_getCursorPrintingPolicy(cursor);
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_TerseOutput, 1);
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_PolishForDeclaration, 1);
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_IncludeTagDefinition, 0);
    std::string text = take(clang_getCursorPrettyPrinted(cursor, policy));
    clang_PrintingPolicy_dispose(policy);
    // An initializer can be a whole lambda; what a hover needs is its start.
    constexpr std::size_t kLongest = 600;
    if (text.size() > kLongest) {
        text.resize(kLongest);
        text += " ...";
    }
    return text;
}

// The value a constant has, where Clang can evaluate it. A variable that is not
// const is left out: its initializer is not the value it has everywhere.
std::string constant_value(CXCursor cursor) {
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_EnumConstantDecl) {
        return std::to_string(clang_getEnumConstantDeclValue(cursor));
    }
    if (kind != CXCursor_VarDecl || clang_isConstQualifiedType(clang_getCursorType(cursor)) == 0) {
        return {};
    }
    CXEvalResult result = clang_Cursor_Evaluate(cursor);
    if (result == nullptr) {
        return {};
    }
    std::string value;
    switch (clang_EvalResult_getKind(result)) {
        case CXEval_Int:
            value = clang_EvalResult_isUnsignedInt(result) != 0
                        ? std::to_string(clang_EvalResult_getAsUnsigned(result))
                        : std::to_string(clang_EvalResult_getAsLongLong(result));
            break;
        case CXEval_Float:
            value = std::to_string(clang_EvalResult_getAsDouble(result));
            break;
        case CXEval_StrLiteral:
            if (const char* text = clang_EvalResult_getAsStr(result)) {
                value = std::string("\"") + text + "\"";
            }
            break;
        default:
            break;
    }
    clang_EvalResult_dispose(result);
    return value;
}

} // namespace

std::optional<Description> EditorUnit::describe(std::size_t offset) const {
    const State& state = *state_;
    if (state.unit == nullptr || offset > state.main.text.size()) {
        return std::nullopt;
    }
    CXFile file = state.main_file();
    if (file == nullptr) {
        return std::nullopt;
    }
    CXToken* token =
        clang_getToken(state.unit, clang_getLocationForOffset(state.unit, file, static_cast<unsigned>(offset)));
    if (token == nullptr) {
        return std::nullopt;
    }
    const CXTokenKind token_kind = clang_getTokenKind(*token);
    const std::string spelling = take(clang_getTokenSpelling(state.unit, *token));
    const CXSourceRange token_extent = clang_getTokenExtent(state.unit, *token);
    clang_disposeTokens(state.unit, token, 1);

    Description description;
    description.named =
        Extent{state.place(clang_getRangeStart(token_extent)), state.place(clang_getRangeEnd(token_extent))};

    CXCursor target = clang_getNullCursor();
    if (token_kind == CXToken_Keyword && (spelling == "auto" || spelling == "decltype")) {
        const CXType deduced = clang_getCursorType(state.cursor_at(offset));
        if (deduced.kind == CXType_Invalid) {
            return std::nullopt;
        }
        description.type = take(clang_getTypeSpelling(deduced));
        target = clang_getTypeDeclaration(pointee_of(deduced));
        if (is_null(target)) {
            description.kind = "type";
            description.name = description.type;
            description.qualified_name = description.type;
            return description;
        }
    } else {
        const std::vector<CXCursor> targets = state.named_at(offset);
        if (targets.empty()) {
            return std::nullopt;
        }
        target = targets.front();
    }

    const CXCursorKind kind = clang_getCursorKind(target);
    description.kind = kind_in_words(kind);
    description.name = take(clang_getCursorSpelling(target));
    description.qualified_name =
        kind == CXCursor_ParmDecl || kind == CXCursor_MacroDefinition ||
                (kind == CXCursor_VarDecl && clang_getCursorSemanticParent(target).kind != CXCursor_TranslationUnit &&
                 clang_getCursorSemanticParent(target).kind != CXCursor_Namespace)
            ? description.name
            : qualified(target);

    const CXCursor definition = clang_getCursorDefinition(target);
    const CXCursor declared = is_null(definition) ? target : definition;
    description.declared = state.name_extent(declared);

    if (kind == CXCursor_MacroDefinition) {
        // A macro is its definition as written.
        const CXSourceRange extent = clang_getCursorExtent(target);
        CXFile macro_file = nullptr;
        unsigned begin = 0;
        unsigned end = 0;
        clang_getFileLocation(clang_getRangeStart(extent), &macro_file, nullptr, nullptr, &begin);
        clang_getFileLocation(clang_getRangeEnd(extent), nullptr, nullptr, nullptr, &end);
        std::size_t size = 0;
        const char* contents = macro_file != nullptr ? clang_getFileContents(state.unit, macro_file, &size) : nullptr;
        if (contents != nullptr && begin < end && end <= size) {
            description.declaration = "#define " + std::string(contents + begin, end - begin);
        }
    } else {
        description.declaration = pretty(declared);
    }

    if (description.type.empty()) {
        if (kind == CXCursor_VarDecl || kind == CXCursor_ParmDecl || kind == CXCursor_FieldDecl) {
            description.type = take(clang_getTypeSpelling(clang_getCursorType(target)));
        } else if (kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl) {
            description.type = take(clang_getTypeSpelling(clang_getTypedefDeclUnderlyingType(target)));
        }
    }
    description.value = constant_value(declared);

    if (kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl || kind == CXCursor_UnionDecl ||
        kind == CXCursor_EnumDecl || kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl) {
        const CXType type = clang_getCursorType(declared);
        const long long size = clang_Type_getSizeOf(type);
        const long long alignment = clang_Type_getAlignOf(type);
        if (size >= 0 && alignment >= 0) {
            description.size = size;
            description.alignment = alignment;
        }
    }

    for (const CXCursor candidate : {target, definition, clang_getCanonicalCursor(target)}) {
        if (!is_null(candidate) && description.documentation.empty()) {
            description.documentation = comment_text(take(clang_Cursor_getRawCommentText(candidate)));
        }
    }
    return description;
}

} // namespace cppl::clangbridge
