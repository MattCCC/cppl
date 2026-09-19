#include "cppl/clang/bridge.hpp"

#include <clang-c/Index.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace cppl::clangbridge {

namespace {

constexpr unsigned kMaxExpressionDepth = 128;

class ScopedString {
public:
    explicit ScopedString(CXString value) : value_(value) {}
    ~ScopedString() { clang_disposeString(value_); }

    ScopedString(const ScopedString&) = delete;
    ScopedString& operator=(const ScopedString&) = delete;
    ScopedString(ScopedString&&) = delete;
    ScopedString& operator=(ScopedString&&) = delete;

    [[nodiscard]] std::string str() const {
        const char* text = clang_getCString(value_);
        return text != nullptr ? std::string(text) : std::string();
    }

private:
    CXString value_;
};

std::string take(CXString value) {
    return ScopedString(value).str();
}

source::SourceLocation presumed_location(CXSourceLocation location) {
    CXString file{};
    unsigned line = 0;
    unsigned column = 0;
    clang_getPresumedLocation(location, &file, &line, &column);

    source::SourceLocation result;
    result.file = take(file);
    result.line = line;
    result.column = column;
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

Type convert_type(CXType type) {
    const CXType canonical = clang_getCanonicalType(type);

    Type converted;
    converted.spelling = take(clang_getTypeSpelling(canonical));

    const long long size = clang_Type_getSizeOf(canonical);
    const bool layout_known = size > 0 && size <= 8;

    switch (canonical.kind) {
        case CXType_Bool:
            converted.kind = TypeKind::Bool;
            break;

        case CXType_Char_S:
        case CXType_SChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long:
        case CXType_LongLong:
            if (layout_known) {
                converted.kind = TypeKind::Int;
                converted.is_signed = true;
                converted.width = static_cast<std::uint16_t>(size * 8);
            }
            break;

        case CXType_Char_U:
        case CXType_UChar:
        case CXType_UShort:
        case CXType_UInt:
        case CXType_ULong:
        case CXType_ULongLong:
            if (layout_known) {
                converted.kind = TypeKind::Int;
                converted.is_signed = false;
                converted.width = static_cast<std::uint16_t>(size * 8);
            }
            break;

        default:
            break;
    }

    return converted;
}

std::string qualified_name_of(CXCursor cursor) {
    std::vector<std::string> parts;
    parts.push_back(take(clang_getCursorSpelling(cursor)));

    CXCursor parent = clang_getCursorSemanticParent(cursor);
    while (!clang_Cursor_isNull(parent) &&
           clang_getCursorKind(parent) != CXCursor_TranslationUnit &&
           !clang_isInvalid(clang_getCursorKind(parent))) {
        std::string name = take(clang_getCursorSpelling(parent));
        if (!name.empty()) {
            parts.push_back(std::move(name));
        }
        const CXCursor next = clang_getCursorSemanticParent(parent);
        if (clang_equalCursors(next, parent) != 0) {
            break;
        }
        parent = next;
    }

    std::string qualified;
    for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
        if (!qualified.empty()) {
            qualified += "::";
        }
        qualified += *part;
    }
    return qualified;
}

Expr unsupported_expression(CXCursor cursor, std::string reason) {
    Expr expr;
    expr.type = convert_type(clang_getCursorType(cursor));
    expr.location = presumed_location(clang_getCursorLocation(cursor));
    expr.node = Unsupported{std::move(reason)};
    return expr;
}

Expr build_expression(CXCursor cursor, const std::vector<CXCursor>& parameters, unsigned depth);

Expr build_integer_literal(CXCursor cursor) {
    CXEvalResult evaluated = clang_Cursor_Evaluate(cursor);
    if (evaluated == nullptr) {
        return unsupported_expression(cursor, "Clang could not evaluate this literal");
    }

    struct Release {
        CXEvalResult result;
        ~Release() { clang_EvalResult_dispose(result); }
    } release{evaluated};

    if (clang_EvalResult_getKind(evaluated) != CXEval_Int) {
        return unsupported_expression(cursor, "literal does not evaluate to an integer");
    }

    std::int64_t value = 0;
    if (clang_EvalResult_isUnsignedInt(evaluated) != 0) {
        const unsigned long long unsigned_value = clang_EvalResult_getAsUnsigned(evaluated);
        if (unsigned_value >
            static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max())) {
            return unsupported_expression(
                cursor, "integer literal is outside the range the formal core represents");
        }
        value = static_cast<std::int64_t>(unsigned_value);
    } else {
        value = static_cast<std::int64_t>(clang_EvalResult_getAsLongLong(evaluated));
    }

    Expr expr;
    expr.type = convert_type(clang_getCursorType(cursor));
    expr.location = presumed_location(clang_getCursorLocation(cursor));
    expr.node = IntLiteral{value};
    return expr;
}

Expr build_expression(CXCursor cursor, const std::vector<CXCursor>& parameters, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return unsupported_expression(cursor, "expression nests deeper than the bridge allows");
    }

    const CXCursorKind kind = clang_getCursorKind(cursor);

    // Nodes Clang inserts that carry no meaning of their own are traversed
    // through, but only while they do not change the type. A node that changes
    // the type is a conversion, and conversions are not modeled yet.
    if (kind == CXCursor_UnexposedExpr || kind == CXCursor_ParenExpr) {
        const std::vector<CXCursor> inner = children_of(cursor);
        if (inner.size() != 1) {
            return unsupported_expression(cursor, "unsupported implicit expression node");
        }
        const CXType outer = clang_getCanonicalType(clang_getCursorType(cursor));
        const CXType nested = clang_getCanonicalType(clang_getCursorType(inner[0]));
        if (clang_equalTypes(outer, nested) == 0) {
            return unsupported_expression(
                cursor, "implicit conversion from '" + take(clang_getTypeSpelling(nested)) +
                            "' to '" + take(clang_getTypeSpelling(outer)) + "' is not modeled");
        }
        return build_expression(inner[0], parameters, depth + 1);
    }

    if (kind == CXCursor_DeclRefExpr) {
        const CXCursor referenced = clang_getCursorReferenced(cursor);
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (clang_equalCursors(referenced, parameters[index]) != 0) {
                Expr expr;
                expr.type = convert_type(clang_getCursorType(cursor));
                expr.location = presumed_location(clang_getCursorLocation(cursor));
                expr.node = ParameterRef{static_cast<std::uint32_t>(index),
                                         take(clang_getCursorSpelling(referenced))};
                return expr;
            }
        }
        return unsupported_expression(cursor,
                                      "'" + take(clang_getCursorSpelling(referenced)) +
                                          "' is not a parameter of the enclosing declaration");
    }

    if (kind == CXCursor_IntegerLiteral) {
        return build_integer_literal(cursor);
    }

    if (kind == CXCursor_CallExpr) {
        const CXCursor referenced = clang_getCursorReferenced(cursor);
        if (clang_Cursor_isNull(referenced) != 0 ||
            clang_getCursorKind(referenced) != CXCursor_FunctionDecl) {
            return unsupported_expression(cursor,
                                          "call does not resolve to an ordinary function");
        }

        Call call;
        call.callee_usr = take(clang_getCursorUSR(referenced));
        call.callee_name = qualified_name_of(referenced);

        const int argument_count = clang_Cursor_getNumArguments(cursor);
        if (argument_count < 0) {
            return unsupported_expression(cursor, "call arguments could not be resolved");
        }
        for (int index = 0; index < argument_count; ++index) {
            call.arguments.push_back(
                build_expression(clang_Cursor_getArgument(cursor, static_cast<unsigned>(index)),
                                 parameters, depth + 1));
        }

        Expr expr;
        expr.type = convert_type(clang_getCursorType(cursor));
        expr.location = presumed_location(clang_getCursorLocation(cursor));
        expr.node = std::move(call);
        return expr;
    }

    if (kind == CXCursor_UnaryOperator &&
        clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_LNot) {
        const auto operands = children_of(cursor);
        if (operands.size() != 1) return unsupported_expression(cursor, "malformed negation");
        Expr expr;
        expr.type = convert_type(clang_getCursorType(cursor));
        expr.location = presumed_location(clang_getCursorLocation(cursor));
        expr.node = Negation{{build_expression(operands[0], parameters, depth + 1)}};
        return expr;
    }

    if (kind == CXCursor_BinaryOperator) {
        const enum CXBinaryOperatorKind op = clang_getCursorBinaryOperatorKind(cursor);
        BinaryOp mapped = BinaryOp::Unsupported;
        if (op == CXBinaryOperator_Add) {
            mapped = BinaryOp::Add;
        } else if (op == CXBinaryOperator_EQ) {
            mapped = BinaryOp::Equal;
        } else if (op == CXBinaryOperator_NE) {
            mapped = BinaryOp::NotEqual;
        } else if (op == CXBinaryOperator_LT) {
            mapped = BinaryOp::Less;
        } else if (op == CXBinaryOperator_LE) {
            mapped = BinaryOp::LessEqual;
        } else if (op == CXBinaryOperator_GT) {
            mapped = BinaryOp::Greater;
        } else if (op == CXBinaryOperator_GE) {
            mapped = BinaryOp::GreaterEqual;
        }
        if (mapped == BinaryOp::Unsupported) {
            return unsupported_expression(
                cursor, "operator '" + take(clang_getBinaryOperatorKindSpelling(op)) +
                            "' is not modeled");
        }

        const std::vector<CXCursor> operands = children_of(cursor);
        if (operands.size() != 2) {
            return unsupported_expression(cursor, "binary operator does not have two operands");
        }

        Binary binary;
        binary.op = mapped;
        binary.operands.push_back(build_expression(operands[0], parameters, depth + 1));
        binary.operands.push_back(build_expression(operands[1], parameters, depth + 1));

        Expr expr;
        expr.type = convert_type(clang_getCursorType(cursor));
        expr.location = presumed_location(clang_getCursorLocation(cursor));
        expr.node = std::move(binary);
        return expr;
    }

    return unsupported_expression(cursor, "'" + take(clang_getCursorKindSpelling(kind)) +
                                              "' is not modeled");
}

std::vector<CXCursor> parameters_of(CXCursor cursor) {
    std::vector<CXCursor> parameters;
    const int count = clang_Cursor_getNumArguments(cursor);
    for (int index = 0; index < count; ++index) {
        parameters.push_back(clang_Cursor_getArgument(cursor, static_cast<unsigned>(index)));
    }
    return parameters;
}

std::size_t return_paths(const Expr& expression) {
    if (const auto* branch = std::get_if<Conditional>(&expression.node)) {
        return return_paths(branch->operands[1]) + return_paths(branch->operands[2]);
    }
    return 1;
}

std::optional<Expr> return_tree(CXCursor cursor, const std::vector<CXCursor>& parameters,
                                std::optional<Expr> continuation, unsigned depth,
                                std::string& rejection, bool& falls_through) {
    if (depth > kMaxExpressionDepth) {
        rejection = "body nests deeper than the bridge allows";
        return std::nullopt;
    }
    const auto kind = clang_getCursorKind(cursor);
    const auto children = children_of(cursor);
    if (kind == CXCursor_CompoundStmt) {
        bool later_statement = false;
        falls_through = true;
        for (auto statement = children.rbegin(); statement != children.rend(); ++statement) {
            bool statement_falls_through = true;
            continuation = return_tree(*statement, parameters, std::move(continuation),
                                       depth + 1, rejection, statement_falls_through);
            if (!rejection.empty()) return std::nullopt;
            if (later_statement && !statement_falls_through) {
                rejection = "unreachable trailing statements are not modeled";
                return std::nullopt;
            }
            falls_through = falls_through && statement_falls_through;
            later_statement = true;
        }
        return continuation;
    }
    if (kind == CXCursor_ReturnStmt) {
        falls_through = false;
        if (children.size() != 1) {
            rejection = "a return requires one value";
            return std::nullopt;
        }
        return build_expression(children[0], parameters, 0);
    }
    if (kind == CXCursor_IfStmt && (children.size() == 2 || children.size() == 3) &&
        clang_isExpression(clang_getCursorKind(children[0])) != 0) {
        bool true_falls = true;
        bool false_falls = true;
        auto when_true = return_tree(children[1], parameters, continuation, depth + 1,
                                     rejection, true_falls);
        if (!rejection.empty()) return std::nullopt;
        auto when_false = continuation;
        if (children.size() == 3) {
            when_false = return_tree(children[2], parameters, continuation, depth + 1,
                                      rejection, false_falls);
            if (!rejection.empty()) return std::nullopt;
        }
        falls_through = true_falls || false_falls;
        if (!when_true || !when_false) {
            rejection = "every path must return a value";
            return std::nullopt;
        }
        if (return_paths(*when_true) + return_paths(*when_false) > 128) {
            rejection = "more than 128 return paths are not modeled";
            return std::nullopt;
        }
        Expr result;
        result.type = when_true->type;
        result.location = presumed_location(clang_getCursorLocation(cursor));
        result.node = Conditional{{build_expression(children[0], parameters, 0),
                                   std::move(*when_true), std::move(*when_false)}};
        return result;
    }
    rejection = "only if/else, blocks, and return statements are modeled; found '" +
                take(clang_getCursorKindSpelling(kind)) + "'";
    return std::nullopt;
}

void extract_body(Function& function, CXCursor cursor, const std::vector<CXCursor>& parameters) {
    const std::vector<CXCursor> members = children_of(cursor);

    std::size_t body_index = members.size();
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (clang_getCursorKind(members[index]) == CXCursor_CompoundStmt) {
            body_index = index;
        }
    }
    if (body_index == members.size()) {
        function.has_body = false;
        return;
    }

    function.has_body = true;

    std::string rejection;
    bool falls_through = true;
    function.returned_value = return_tree(members[body_index], parameters, std::nullopt, 0,
                                          rejection, falls_through);
    if (!function.returned_value) {
        function.body_rejection = rejection.empty() ? "every path must return a value" : rejection;
    }
}

bool matches_location(const source::SourceLocation& declaration,
                      const source::SourceLocation& wanted) {
    if (declaration.file != wanted.file || declaration.line != wanted.line) {
        return false;
    }
    return wanted.column == 0 || declaration.column == wanted.column;
}

struct Collector {
    const Selection* selection = nullptr;
    std::vector<CXCursor> selected;
};

bool is_selected(CXCursor cursor, const Selection& selection) {
    const std::string name = take(clang_getCursorSpelling(cursor));
    if (!selection.specification_prefix.empty() && name.starts_with(selection.specification_prefix)) {
        return true;
    }

    const source::SourceLocation location = presumed_location(clang_getCursorLocation(cursor));
    return std::ranges::any_of(selection.locations, [&](const source::SourceLocation& wanted) {
        return matches_location(location, wanted);
    });
}

CXChildVisitResult collect(CXCursor cursor, CXCursor, CXClientData data) {
    auto& collector = *static_cast<Collector*>(data);
    const CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind == CXCursor_Namespace || kind == CXCursor_UnexposedDecl ||
        kind == CXCursor_LinkageSpec) {
        return CXChildVisit_Recurse;
    }

    if (kind == CXCursor_FunctionDecl && is_selected(cursor, *collector.selection)) {
        collector.selected.push_back(cursor);
    }

    return CXChildVisit_Continue;
}

Severity convert_severity(CXDiagnosticSeverity severity) {
    switch (severity) {
        case CXDiagnostic_Ignored:
        case CXDiagnostic_Note:
            return Severity::Note;
        case CXDiagnostic_Warning:
            return Severity::Warning;
        case CXDiagnostic_Error:
            return Severity::Error;
        case CXDiagnostic_Fatal:
            return Severity::Fatal;
    }
    return Severity::Error;
}

}  // namespace

const Function* TranslationUnit::find_by_usr(std::string_view usr) const {
    for (const Function& function : functions) {
        if (function.usr == usr) {
            return &function;
        }
    }
    return nullptr;
}

const Function* TranslationUnit::find_by_name(std::string_view name) const {
    for (const Function& function : functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return nullptr;
}

const Function* TranslationUnit::find_at(const source::SourceLocation& location) const {
    for (const Function& function : functions) {
        if (matches_location(function.location, location)) {
            return &function;
        }
    }
    return nullptr;
}

std::expected<TranslationUnit, std::string> parse(const ParseRequest& request) {
    CXIndex index = clang_createIndex(/*excludeDeclarationsFromPCH=*/0, /*displayDiagnostics=*/0);
    if (index == nullptr) {
        return std::unexpected("could not create a Clang index");
    }

    struct ReleaseIndex {
        CXIndex index;
        ~ReleaseIndex() { clang_disposeIndex(index); }
    } release_index{index};

    std::vector<const char*> argv;
    argv.reserve(request.arguments.size());
    for (const std::string& argument : request.arguments) {
        argv.push_back(argument.c_str());
    }

    CXTranslationUnit unit = nullptr;
    const CXErrorCode error =
        clang_parseTranslationUnit2(index, request.path.c_str(), argv.data(),
                                    static_cast<int>(argv.size()), nullptr, 0,
                                    CXTranslationUnit_None, &unit);
    if (error != CXError_Success || unit == nullptr) {
        return std::unexpected("Clang failed to parse '" + request.path + "'");
    }

    struct ReleaseUnit {
        CXTranslationUnit unit;
        ~ReleaseUnit() { clang_disposeTranslationUnit(unit); }
    } release_unit{unit};

    TranslationUnit result;

    const unsigned diagnostic_count = clang_getNumDiagnostics(unit);
    for (unsigned index_of_diagnostic = 0; index_of_diagnostic < diagnostic_count;
         ++index_of_diagnostic) {
        CXDiagnostic diagnostic = clang_getDiagnostic(unit, index_of_diagnostic);
        Diagnostic converted;
        converted.severity = convert_severity(clang_getDiagnosticSeverity(diagnostic));
        converted.message = take(clang_getDiagnosticSpelling(diagnostic));
        converted.location = presumed_location(clang_getDiagnosticLocation(diagnostic));
        clang_disposeDiagnostic(diagnostic);

        if (converted.severity == Severity::Error || converted.severity == Severity::Fatal) {
            result.has_errors = true;
        }
        result.diagnostics.push_back(std::move(converted));
    }

    Collector collector;
    collector.selection = &request.selection;
    clang_visitChildren(clang_getTranslationUnitCursor(unit), collect, &collector);

    for (const CXCursor& cursor : collector.selected) {
        Function function;
        function.usr = take(clang_getCursorUSR(cursor));
        function.name = take(clang_getCursorSpelling(cursor));
        function.qualified_name = qualified_name_of(cursor);
        function.result = convert_type(clang_getCursorResultType(cursor));
        function.location = presumed_location(clang_getCursorLocation(cursor));

        const std::vector<CXCursor> parameter_cursors = parameters_of(cursor);
        for (const CXCursor& parameter : parameter_cursors) {
            function.parameters.push_back(Parameter{take(clang_getCursorSpelling(parameter)),
                                                    convert_type(clang_getCursorType(parameter))});
        }

        extract_body(function, cursor, parameter_cursors);
        result.functions.push_back(std::move(function));
    }

    return result;
}

std::string clang_version() {
    return take(clang_getClangVersion());
}

}  // namespace cppl::clangbridge
