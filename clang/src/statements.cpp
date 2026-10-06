#include "statements.hpp"

#include <algorithm>
#include <clang-c/CXFile.h>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/CXString.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace cppl::clangbridge::detail {

namespace {

// How deep a statement or expression is followed, as the bridge bounds it.
constexpr unsigned kMaxStatementDepth = 128;

std::string take(CXString value) {
    const char* text = clang_getCString(value);
    std::string result = text != nullptr ? std::string(text) : std::string();
    clang_disposeString(value);
    return result;
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

FilePosition file_position(CXSourceLocation location) {
    FilePosition position;
    clang_getFileLocation(location, &position.file, nullptr, nullptr, &position.offset);
    return position;
}

} // namespace

bool is_switch_label(CXCursor statement) {
    const CXCursorKind kind = clang_getCursorKind(statement);
    return kind == CXCursor_CaseStmt || kind == CXCursor_DefaultStmt;
}

bool holds_switch_label(CXCursor root, unsigned depth) {
    if (depth > kMaxStatementDepth) {
        return true;
    }
    const CXCursorKind kind = clang_getCursorKind(root);
    if (kind == CXCursor_SwitchStmt || kind == CXCursor_LambdaExpr) {
        return false;
    }
    return std::ranges::any_of(children_of(root), [depth](CXCursor child) {
        return is_switch_label(child) || holds_switch_label(child, depth + 1);
    });
}

FilePosition start_of(CXCursor cursor) {
    return file_position(clang_getRangeStart(clang_getCursorExtent(cursor)));
}

bool before(const FilePosition& position, CXFile file, unsigned offset) {
    return position.file != nullptr && clang_File_isEqual(position.file, file) != 0 && position.offset < offset;
}

bool stands_at(const FilePosition& position, CXFile file, unsigned offset) {
    return position.file != nullptr && clang_File_isEqual(position.file, file) != 0 && position.offset == offset;
}

std::optional<SelectionHead> selection_head(CXCursor statement) {
    const std::vector<CXCursor> parts = children_of(statement);
    if (parts.empty()) {
        return std::nullopt;
    }
    CXTranslationUnit unit = clang_Cursor_getTranslationUnit(statement);
    const CXSourceRange range = clang_getRange(clang_getRangeStart(clang_getCursorExtent(statement)),
                                               clang_getRangeStart(clang_getCursorExtent(parts.back())));
    CXToken* tokens = nullptr;
    unsigned count = 0;
    clang_tokenize(unit, range, &tokens, &count);
    SelectionHead head;
    bool read = false;
    std::size_t nesting = 0;
    for (unsigned index = 0; index < count && !read; ++index) {
        const std::string spelled = take(clang_getTokenSpelling(unit, tokens[index]));
        if (index == 1 && spelled == "constexpr") {
            head.constant = true;
        }
        if ((index == 1 || index == 2) && spelled == "consteval") {
            head.immediate = true;
            read = true;
            break;
        }
        const FilePosition at = file_position(clang_getTokenLocation(unit, tokens[index]));
        if (nesting == 1 && head.file == nullptr) {
            head.file = at.file;
            head.first = at.offset;
        }
        if (clang_getTokenKind(tokens[index]) != CXToken_Punctuation) {
            continue;
        }
        if (spelled == "(" || spelled == "[" || spelled == "{") {
            ++nesting;
        } else if ((spelled == ")" || spelled == "]" || spelled == "}") && nesting > 0) {
            if (--nesting == 0) {
                head.close = at.offset;
                read = head.file != nullptr && at.file != nullptr && clang_File_isEqual(at.file, head.file) != 0;
                break;
            }
        } else if (spelled == ";" && nesting == 1) {
            if (!head.separator.has_value()) {
                head.separator = at.offset;
            }
        }
    }
    clang_disposeTokens(unit, tokens, count);
    if (!read) {
        return std::nullopt;
    }
    return head;
}

bool is_fallthrough(CXCursor statement) {
    if (clang_getCursorKind(statement) != CXCursor_UnexposedStmt) {
        return false;
    }
    const std::vector<CXCursor> parts = children_of(statement);
    if (parts.size() != 1 || clang_getCursorKind(parts.front()) != CXCursor_NullStmt) {
        return false;
    }
    CXTranslationUnit unit = clang_Cursor_getTranslationUnit(statement);
    CXToken* tokens = nullptr;
    unsigned count = 0;
    clang_tokenize(unit, clang_getCursorExtent(statement), &tokens, &count);
    std::vector<std::string> spelled;
    spelled.reserve(count);
    for (unsigned index = 0; index < count; ++index) {
        spelled.push_back(take(clang_getTokenSpelling(unit, tokens[index])));
    }
    clang_disposeTokens(unit, tokens, count);
    if (!spelled.empty() && spelled.back() == ";") {
        spelled.pop_back();
    }
    const std::vector<std::string> standard{"[", "[", "fallthrough", "]", "]"};
    const std::vector<std::string> qualified{"[", "[", "clang", "::", "fallthrough", "]", "]"};
    return spelled == standard || spelled == qualified;
}

} // namespace cppl::clangbridge::detail
