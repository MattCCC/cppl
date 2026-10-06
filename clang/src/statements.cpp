#include "statements.hpp"

#include "cppl/clang/ast.hpp"
#include "places.hpp"

#include <algorithm>
#include <clang-c/CXFile.h>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/CXString.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
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

std::size_t file_offset(CXSourceLocation location) {
    unsigned offset = 0;
    clang_getFileLocation(location, nullptr, nullptr, nullptr, &offset);
    return static_cast<std::size_t>(offset);
}

Expr short_circuit(Expr value, unsigned depth = 0) {
    if (depth > kMaxStatementDepth) {
        return value;
    }
    std::visit(
        [depth](auto& node) {
            if constexpr (requires { node.operands; }) {
                for (Expr& operand : node.operands) {
                    operand = short_circuit(std::move(operand), depth + 1);
                }
            } else if constexpr (requires { node.arguments; }) {
                for (Expr& argument : node.arguments) {
                    argument = short_circuit(std::move(argument), depth + 1);
                }
            }
        },
        value.node);
    auto* binary = std::get_if<Binary>(&value.node);
    if (binary == nullptr || (binary->op != BinaryOp::And && binary->op != BinaryOp::Or) ||
        binary->operands.size() != 2 || value.type.kind != TypeKind::Bool) {
        return value;
    }
    const bool conjunction = binary->op == BinaryOp::And;
    Expr constant;
    constant.type = value.type;
    constant.location = value.location;
    constant.node = IntLiteral{conjunction ? 0 : 1};
    Expr first = std::move(binary->operands[0]);
    Expr second = std::move(binary->operands[1]);
    Expr chosen;
    chosen.type = value.type;
    chosen.location = value.location;
    chosen.node = conjunction ? Conditional{{std::move(first), std::move(second), std::move(constant)}}
                              : Conditional{{std::move(first), std::move(constant), std::move(second)}};
    return chosen;
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

std::optional<ForParts> for_parts(CXCursor statement) {
    const std::vector<CXCursor> children = children_of(statement);
    if (children.empty()) {
        return std::nullopt;
    }
    CXTranslationUnit unit = clang_Cursor_getTranslationUnit(statement);
    CXToken* tokens = nullptr;
    unsigned count = 0;
    clang_tokenize(unit, clang_getCursorExtent(statement), &tokens, &count);
    struct Release {
        CXTranslationUnit unit;
        CXToken* tokens;
        unsigned count;
        ~Release() {
            if (tokens != nullptr) {
                clang_disposeTokens(unit, tokens, count);
            }
        }
    } release{unit, tokens, count};

    std::vector<std::size_t> separators;
    int nesting = 0;
    for (unsigned index = 0; index < count; ++index) {
        if (clang_getTokenKind(tokens[index]) != CXToken_Punctuation) {
            continue;
        }
        const std::string spelling = take(clang_getTokenSpelling(unit, tokens[index]));
        if (spelling == "(" || spelling == "[" || spelling == "{") {
            ++nesting;
        } else if (spelling == ")" || spelling == "]" || spelling == "}") {
            if (--nesting == 0) {
                break;
            }
        } else if (spelling == ";" && nesting == 1) {
            separators.push_back(file_offset(clang_getTokenLocation(unit, tokens[index])));
        }
    }
    if (separators.size() != 2) {
        return std::nullopt;
    }

    ForParts parts;
    parts.body = children.back();
    for (std::size_t index = 0; index + 1 < children.size(); ++index) {
        const std::size_t start = file_offset(clang_getRangeStart(clang_getCursorExtent(children[index])));
        std::optional<CXCursor>& part = start < separators[0]   ? parts.initialization
                                        : start < separators[1] ? parts.condition
                                                                : parts.increment;
        if (part.has_value()) {
            return std::nullopt;
        }
        part = children[index];
    }
    return parts;
}

std::optional<SelectedValue> selected_value(CXCursor value) {
    for (std::vector<CXCursor> wrapped = children_of(value);
         (clang_getCursorKind(value) == CXCursor_ParenExpr || clang_getCursorKind(value) == CXCursor_UnexposedExpr) &&
         wrapped.size() == 1 &&
         bridge::same_modeled_value(bridge::convert_type(clang_getCursorType(value)),
                                    bridge::convert_type(clang_getCursorType(wrapped.front())));
         wrapped = children_of(value)) {
        value = wrapped.front();
    }
    const std::vector<CXCursor> parts = children_of(value);
    const Type type = bridge::convert_type(clang_getCursorType(value));
    if (type.kind != TypeKind::Int && type.kind != TypeKind::Bool) {
        return std::nullopt;
    }
    SelectedValue selected{parts.empty() ? value : parts.front(), std::nullopt, std::nullopt, {}, value};
    selected.constant.type = type;
    selected.constant.location = bridge::presumed_location(clang_getCursorLocation(value));
    if (clang_getCursorKind(value) == CXCursor_ConditionalOperator && parts.size() == 3) {
        selected.when_true = parts[1];
        selected.when_false = parts[2];
        return selected;
    }
    if (clang_getCursorKind(value) != CXCursor_BinaryOperator || parts.size() != 2) {
        return std::nullopt;
    }
    if (clang_getCursorBinaryOperatorKind(value) == CXBinaryOperator_LAnd) {
        selected.when_true = parts[1];
        selected.constant.node = IntLiteral{0};
        return selected;
    }
    if (clang_getCursorBinaryOperatorKind(value) == CXBinaryOperator_LOr) {
        selected.when_false = parts[1];
        selected.constant.node = IntLiteral{1};
        return selected;
    }
    return std::nullopt;
}

namespace {

// Whether `cursor` reads storage through a subscript or a pointer anywhere.
bool reads_through(CXCursor cursor, unsigned depth = 0) {
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (depth > kMaxStatementDepth || kind == CXCursor_ArraySubscriptExpr ||
        (kind == CXCursor_UnaryOperator && clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_Deref) ||
        (kind == CXCursor_CallExpr && take(clang_getCursorSpelling(cursor)) == "operator[]")) {
        return true;
    }
    return std::ranges::any_of(children_of(cursor),
                               [depth](CXCursor child) { return reads_through(child, depth + 1); });
}

} // namespace

std::optional<SelectedValue> selected_reading(CXCursor statement) {
    std::optional<CXCursor> value;
    const std::vector<CXCursor> parts = children_of(statement);
    if (clang_getCursorKind(statement) == CXCursor_DeclStmt && parts.size() == 1 &&
        clang_getCursorKind(parts.front()) == CXCursor_VarDecl &&
        clang_Cursor_isNull(clang_Cursor_getVarDeclInitializer(parts.front())) == 0) {
        value = clang_Cursor_getVarDeclInitializer(parts.front());
    } else if (clang_getCursorKind(statement) == CXCursor_BinaryOperator && parts.size() == 2 &&
               clang_getCursorBinaryOperatorKind(statement) == CXBinaryOperator_Assign) {
        value = parts[1];
    }
    std::optional<SelectedValue> selected = value ? selected_value(*value) : std::nullopt;
    if (!selected || !((selected->when_true && reads_through(*selected->when_true)) ||
                       (selected->when_false && reads_through(*selected->when_false)))) {
        return std::nullopt;
    }
    return selected;
}

const ChosenArm* chosen_for(const std::vector<ChosenArm>& chosen, CXCursor selection) {
    const auto at = std::ranges::find_if(
        chosen, [&](const ChosenArm& entry) { return clang_equalCursors(entry.selection, selection) != 0; });
    return at == chosen.end() ? nullptr : &*at;
}

Expr runtime_value(Expr value, bool clause) {
    return clause ? std::move(value) : short_circuit(std::move(value));
}

} // namespace cppl::clangbridge::detail
