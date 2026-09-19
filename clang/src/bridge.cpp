#include "cppl/clang/bridge.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstdint>
#include <limits>
#include <utility>

namespace cppl::clangbridge {

namespace {

constexpr unsigned kMaxExpressionDepth = 128;
constexpr std::size_t kMaxReturnPaths = 128;

class ScopedString {
  public:
    explicit ScopedString(CXString value) : value_(value) {}
    ~ScopedString() {
        clang_disposeString(value_);
    }

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

    // A volatile glvalue is read for its effect, not for a value that is a
    // function of anything C++L models, so it is not a modeled type at all
    // (AGENTS.md 11). `const` is not such a qualifier: it constrains writes,
    // and the value read is the ordinary one.
    if (clang_isVolatileQualifiedType(canonical) != 0) {
        return converted;
    }

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
    while (!clang_Cursor_isNull(parent) && clang_getCursorKind(parent) != CXCursor_TranslationUnit &&
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

// A local's identity is the declaration Clang resolved; its current logical
// version is what a read of it denotes. Shadowing needs no rule of its own,
// because an inner declaration is a different declaration.
struct Local {
    CXCursor declaration;
    std::uint32_t version = 0;
    Type type;
};

using Locals = std::vector<Local>;

std::optional<std::size_t> find_local(const Locals& locals, CXCursor declaration) {
    for (std::size_t index = locals.size(); index > 0; --index) {
        if (clang_equalCursors(locals[index - 1].declaration, declaration) != 0) {
            return index - 1;
        }
    }
    return std::nullopt;
}

// Whether two Clang types denote the same modeled value. Qualifiers are not
// part of a value, so a read of a `const` local is the value it holds; two
// spellings that Clang laid out identically are the same machine integer. A
// type C++L does not model is never "the same" as anything.
bool same_modeled_value(const Type& outer, const Type& inner) {
    return outer.kind != TypeKind::Unsupported && outer.kind == inner.kind && outer.width == inner.width &&
           outer.is_signed == inner.is_signed;
}

// Whether C++ performs arithmetic on this type only after promoting it to
// `int`. An update of such a local converts the promoted result back, which is
// a conversion C++L does not model.
bool promoted_before_arithmetic(CXType type) {
    switch (clang_getCanonicalType(type).kind) {
        case CXType_Int:
        case CXType_UInt:
        case CXType_Long:
        case CXType_ULong:
        case CXType_LongLong:
        case CXType_ULongLong:
            return false;
        default:
            return true;
    }
}

std::string unmodeled_statement(const std::string& found) {
    return "only if/else, blocks, local declarations, assignments, and return statements are modeled; found " + found;
}

Expr build_expression(CXCursor cursor, const std::vector<CXCursor>& parameters, const Locals& locals, unsigned depth);

Expr build_integer_literal(CXCursor cursor) {
    CXEvalResult evaluated = clang_Cursor_Evaluate(cursor);
    if (evaluated == nullptr) {
        return unsupported_expression(cursor, "Clang could not evaluate this literal");
    }

    struct Release {
        CXEvalResult result;
        ~Release() {
            clang_EvalResult_dispose(result);
        }
    } release{evaluated};

    if (clang_EvalResult_getKind(evaluated) != CXEval_Int) {
        return unsupported_expression(cursor, "literal does not evaluate to an integer");
    }

    std::int64_t value = 0;
    if (clang_EvalResult_isUnsignedInt(evaluated) != 0) {
        const unsigned long long unsigned_value = clang_EvalResult_getAsUnsigned(evaluated);
        if (unsigned_value > static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max())) {
            return unsupported_expression(cursor, "integer literal is outside the range the formal core represents");
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

Expr build_expression(CXCursor cursor, const std::vector<CXCursor>& parameters, const Locals& locals, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return unsupported_expression(cursor, "expression nests deeper than the bridge allows");
    }

    const CXCursorKind kind = clang_getCursorKind(cursor);

    // Nodes Clang inserts that carry no meaning of their own are traversed
    // through, but only while they do not change the value. A node that changes
    // the value is a conversion, and conversions are not modeled yet.
    if (kind == CXCursor_UnexposedExpr || kind == CXCursor_ParenExpr) {
        const std::vector<CXCursor> inner = children_of(cursor);
        if (inner.size() != 1) {
            return unsupported_expression(cursor, "unsupported implicit expression node");
        }
        const CXType outer = clang_getCanonicalType(clang_getCursorType(cursor));
        const CXType nested = clang_getCanonicalType(clang_getCursorType(inner[0]));
        if (clang_equalTypes(outer, nested) == 0 && !same_modeled_value(convert_type(outer), convert_type(nested))) {
            return unsupported_expression(cursor, "implicit conversion from '" + take(clang_getTypeSpelling(nested)) +
                                                      "' to '" + take(clang_getTypeSpelling(outer)) +
                                                      "' is not modeled");
        }
        return build_expression(inner[0], parameters, locals, depth + 1);
    }

    if (kind == CXCursor_DeclRefExpr) {
        const CXCursor referenced = clang_getCursorReferenced(cursor);
        if (const std::optional<std::size_t> local = find_local(locals, referenced)) {
            Expr expr;
            expr.type = locals[*local].type;
            expr.location = presumed_location(clang_getCursorLocation(cursor));
            expr.node = LocalRef{locals[*local].version, take(clang_getCursorSpelling(referenced))};
            return expr;
        }
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (clang_equalCursors(referenced, parameters[index]) != 0) {
                Expr expr;
                expr.type = convert_type(clang_getCursorType(cursor));
                expr.location = presumed_location(clang_getCursorLocation(cursor));
                expr.node = ParameterRef{static_cast<std::uint32_t>(index), take(clang_getCursorSpelling(referenced))};
                return expr;
            }
        }
        // C++ puts a local in scope inside its own initializer, so scoping
        // alone does not rule out a read before the local holds a value.
        if (clang_getCursorKind(referenced) == CXCursor_VarDecl &&
            clang_Cursor_hasVarDeclGlobalStorage(referenced) == 0) {
            return unsupported_expression(cursor, "local '" + take(clang_getCursorSpelling(referenced)) +
                                                      "' is read where it holds no modeled value, such as in its own "
                                                      "initializer");
        }
        return unsupported_expression(cursor, "'" + take(clang_getCursorSpelling(referenced)) +
                                                  "' is not a parameter or local of the enclosing "
                                                  "declaration");
    }

    if (kind == CXCursor_IntegerLiteral) {
        return build_integer_literal(cursor);
    }

    if (kind == CXCursor_CallExpr) {
        const CXCursor referenced = clang_getCursorReferenced(cursor);
        if (clang_Cursor_isNull(referenced) != 0 || clang_getCursorKind(referenced) != CXCursor_FunctionDecl) {
            return unsupported_expression(cursor, "call does not resolve to an ordinary function");
        }

        Call call;
        call.callee_usr = take(clang_getCursorUSR(referenced));
        call.callee_name = qualified_name_of(referenced);

        const int argument_count = clang_Cursor_getNumArguments(cursor);
        if (argument_count < 0) {
            return unsupported_expression(cursor, "call arguments could not be resolved");
        }
        for (int index = 0; index < argument_count; ++index) {
            call.arguments.push_back(build_expression(clang_Cursor_getArgument(cursor, static_cast<unsigned>(index)),
                                                      parameters, locals, depth + 1));
        }

        Expr expr;
        expr.type = convert_type(clang_getCursorType(cursor));
        expr.location = presumed_location(clang_getCursorLocation(cursor));
        expr.node = std::move(call);
        return expr;
    }

    if (kind == CXCursor_UnaryOperator && clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_LNot) {
        const auto operands = children_of(cursor);
        if (operands.size() != 1)
            return unsupported_expression(cursor, "malformed negation");
        Expr expr;
        expr.type = convert_type(clang_getCursorType(cursor));
        expr.location = presumed_location(clang_getCursorLocation(cursor));
        expr.node = Negation{{build_expression(operands[0], parameters, locals, depth + 1)}};
        return expr;
    }

    if (kind == CXCursor_BinaryOperator) {
        const enum CXBinaryOperatorKind op = clang_getCursorBinaryOperatorKind(cursor);
        BinaryOp mapped = BinaryOp::Unsupported;
        if (op == CXBinaryOperator_Add) {
            mapped = BinaryOp::Add;
        } else if (op == CXBinaryOperator_Sub) {
            mapped = BinaryOp::Sub;
        } else if (op == CXBinaryOperator_Mul) {
            mapped = BinaryOp::Mul;
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
            return unsupported_expression(cursor, "operator '" + take(clang_getBinaryOperatorKindSpelling(op)) +
                                                      "' is not modeled");
        }

        const std::vector<CXCursor> operands = children_of(cursor);
        if (operands.size() != 2) {
            return unsupported_expression(cursor, "binary operator does not have two operands");
        }

        Binary binary;
        binary.op = mapped;
        binary.operands.push_back(build_expression(operands[0], parameters, locals, depth + 1));
        binary.operands.push_back(build_expression(operands[1], parameters, locals, depth + 1));

        Expr expr;
        expr.type = convert_type(clang_getCursorType(cursor));
        expr.location = presumed_location(clang_getCursorLocation(cursor));
        expr.node = std::move(binary);
        return expr;
    }

    return unsupported_expression(cursor, "'" + take(clang_getCursorKindSpelling(kind)) + "' is not modeled");
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
    if (const auto* bound = std::get_if<LocalVersion>(&expression.node)) {
        return return_paths(bound->operands[1]);
    }
    return 1;
}

// Whether control leaves the function rather than reaching what follows. It
// decides reachability only; what each statement means is decided by the
// lowering below.
bool terminates(CXCursor statement, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return false;
    }
    const CXCursorKind kind = clang_getCursorKind(statement);
    if (kind == CXCursor_ReturnStmt) {
        return true;
    }
    if (kind == CXCursor_CompoundStmt) {
        const std::vector<CXCursor> nested = children_of(statement);
        return std::ranges::any_of(nested, [depth](CXCursor child) { return terminates(child, depth + 1); });
    }
    if (kind == CXCursor_IfStmt) {
        const std::vector<CXCursor> parts = children_of(statement);
        return parts.size() == 3 && terminates(parts[1], depth + 1) && terminates(parts[2], depth + 1);
    }
    return false;
}

// What remains to be executed after the statement being lowered: the rest of
// its block, and whatever follows the blocks enclosing it. A branch lowers this
// continuation once per arm, under the versions that arm established, which is
// what makes a local's value path-sensitive without any merge rule.
struct Continuation {
    const Continuation* outer = nullptr;
    const std::vector<CXCursor>* statements = nullptr;
    std::size_t index = 0;
};

// Lowers a resolved function body into the value it returns.
//
// Statements are taken in program order, threading the logical version of each
// local. A declaration or an assignment binds the next version and the rest of
// the body is lowered under it; a read of a local denotes the version current
// where the read stands. Nothing here rewrites the program: the versions are a
// model of the body Clang resolved (SPEC.md 12.8).
struct BodyLowering {
    const std::vector<CXCursor>& parameters;
    std::uint32_t next_version = 0;
    std::string rejection;

    std::nullopt_t reject(std::string reason) {
        if (rejection.empty()) {
            rejection = std::move(reason);
        }
        return std::nullopt;
    }

    Expr bind(std::uint32_t version, std::string name, Expr value, Expr body, CXCursor at) {
        Expr expr;
        expr.type = body.type;
        expr.location = presumed_location(clang_getCursorLocation(at));
        expr.node = LocalVersion{version, std::move(name), {std::move(value), std::move(body)}};
        return expr;
    }

    std::optional<Expr> lower_statements(const Continuation& from, const Locals& locals, unsigned depth) {
        // Each statement lowers the rest of the body inside itself, so this
        // bounds the statements on one path as well as their nesting.
        if (depth > kMaxExpressionDepth) {
            return reject("more than " + std::to_string(kMaxExpressionDepth) +
                          " nested or consecutive statements on one path are not modeled");
        }
        if (from.index == from.statements->size()) {
            if (from.outer == nullptr) {
                return reject("every path must return a value");
            }
            return lower_statements(*from.outer, locals, depth + 1);
        }
        const CXCursor statement = (*from.statements)[from.index];
        const Continuation next{from.outer, from.statements, from.index + 1};
        if (next.index != from.statements->size() && terminates(statement, 0)) {
            return reject("unreachable trailing statements are not modeled");
        }
        return lower_statement(statement, next, locals, depth);
    }

    std::optional<Expr> lower_statement(CXCursor statement, const Continuation& next, const Locals& locals,
                                        unsigned depth) {
        const CXCursorKind kind = clang_getCursorKind(statement);
        if (kind == CXCursor_CompoundStmt) {
            const std::vector<CXCursor> nested = children_of(statement);
            return lower_statements(Continuation{&next, &nested, 0}, locals, depth + 1);
        }
        if (kind == CXCursor_ReturnStmt) {
            const std::vector<CXCursor> returned = children_of(statement);
            if (returned.size() != 1) {
                return reject("a return requires one value");
            }
            return build_expression(returned[0], parameters, locals, 0);
        }
        if (kind == CXCursor_DeclStmt) {
            return lower_declaration(children_of(statement), 0, next, locals, depth);
        }
        if (kind == CXCursor_BinaryOperator &&
            clang_getCursorBinaryOperatorKind(statement) == CXBinaryOperator_Assign) {
            return lower_assignment(statement, next, locals, depth);
        }
        if (kind == CXCursor_CompoundAssignOperator || kind == CXCursor_UnaryOperator) {
            return lower_update(statement, next, locals, depth);
        }
        const std::vector<CXCursor> parts = children_of(statement);
        if (kind == CXCursor_IfStmt && (parts.size() == 2 || parts.size() == 3) &&
            clang_isExpression(clang_getCursorKind(parts[0])) != 0) {
            return lower_branch(statement, parts, next, locals, depth);
        }
        return reject(unmodeled_statement("'" + take(clang_getCursorKindSpelling(kind)) + "'"));
    }

    std::optional<Expr> lower_branch(CXCursor statement, const std::vector<CXCursor>& parts, const Continuation& next,
                                     const Locals& locals, unsigned depth) {
        Expr condition = build_expression(parts[0], parameters, locals, 0);
        std::optional<Expr> when_true = lower_statement(parts[1], next, locals, depth + 1);
        if (!when_true) {
            return std::nullopt;
        }
        std::optional<Expr> when_false = parts.size() == 3 ? lower_statement(parts[2], next, locals, depth + 1)
                                                           : lower_statements(next, locals, depth + 1);
        if (!when_false) {
            return std::nullopt;
        }
        if (return_paths(*when_true) + return_paths(*when_false) > kMaxReturnPaths) {
            return reject("more than " + std::to_string(kMaxReturnPaths) + " return paths are not modeled");
        }
        Expr result;
        result.type = when_true->type;
        result.location = presumed_location(clang_getCursorLocation(statement));
        result.node = Conditional{{std::move(condition), std::move(*when_true), std::move(*when_false)}};
        return result;
    }

    std::optional<Expr> lower_declaration(const std::vector<CXCursor>& declared, std::size_t index,
                                          const Continuation& next, const Locals& locals, unsigned depth) {
        if (index == declared.size()) {
            return lower_statements(next, locals, depth + 1);
        }
        const CXCursor declaration = declared[index];
        const std::string name = take(clang_getCursorSpelling(declaration));
        if (clang_getCursorKind(declaration) != CXCursor_VarDecl) {
            return reject("only variable declarations are modeled inside a verified body; found '" +
                          take(clang_getCursorKindSpelling(clang_getCursorKind(declaration))) + "'");
        }
        const enum CX_StorageClass storage = clang_Cursor_getStorageClass(declaration);
        if (storage != CX_SC_None && storage != CX_SC_Auto) {
            return reject("local '" + name + "' does not have automatic storage");
        }
        if (clang_getCursorTLSKind(declaration) != CXTLS_None) {
            return reject("thread-local '" + name + "' is not modeled");
        }
        const Type type = convert_type(clang_getCursorType(declaration));
        if (type.kind == TypeKind::Unsupported) {
            return reject("local '" + name + "' has type '" + type.spelling + "', which is not modeled");
        }
        CXCursor initializer = clang_Cursor_getVarDeclInitializer(declaration);
        if (clang_Cursor_isNull(initializer) != 0) {
            return reject("local '" + name + "' is declared without an initializer, so it holds no modeled value");
        }
        if (clang_getCursorKind(initializer) == CXCursor_InitListExpr) {
            const std::vector<CXCursor> elements = children_of(initializer);
            if (elements.size() != 1) {
                return reject("the initializer of '" + name + "' is not a single modeled value");
            }
            initializer = elements[0];
        }
        Expr value = build_expression(initializer, parameters, locals, 0);
        if (!std::holds_alternative<Unsupported>(value.node) && !same_modeled_value(type, value.type)) {
            return reject("initializing '" + name + "' of type '" + type.spelling + "' from '" + value.type.spelling +
                          "' is a conversion that is not modeled");
        }
        const std::uint32_t version = next_version++;
        Locals declaring = locals;
        declaring.push_back(Local{declaration, version, type});
        std::optional<Expr> body = lower_declaration(declared, index + 1, next, declaring, depth);
        if (!body) {
            return std::nullopt;
        }
        return bind(version, name, std::move(value), std::move(*body), declaration);
    }

    // The local a write targets. Only a local of this body is ever written.
    std::optional<std::size_t> written_local(CXCursor target, const Locals& locals) {
        while (clang_getCursorKind(target) == CXCursor_ParenExpr) {
            const std::vector<CXCursor> inner = children_of(target);
            if (inner.size() != 1) {
                return reject("this assignment target is not modeled");
            }
            target = inner[0];
        }
        if (clang_getCursorKind(target) != CXCursor_DeclRefExpr) {
            return reject("only a local variable is assigned in a modeled body");
        }
        const CXCursor declaration = clang_getCursorReferenced(target);
        const std::string name = take(clang_getCursorSpelling(declaration));
        const std::optional<std::size_t> local = find_local(locals, declaration);
        if (!local) {
            if (clang_getCursorKind(declaration) == CXCursor_ParmDecl) {
                return reject("assigning to parameter '" + name +
                              "' is not modeled: a contract names the value the caller passed");
            }
            return reject("'" + name + "' is not a local of this body");
        }
        return local;
    }

    std::optional<Expr> write(std::size_t local, Expr value, CXCursor statement, const Continuation& next,
                              const Locals& locals, unsigned depth) {
        const std::uint32_t version = next_version++;
        Locals assigned = locals;
        assigned[local].version = version;
        std::optional<Expr> body = lower_statements(next, assigned, depth + 1);
        if (!body) {
            return std::nullopt;
        }
        return bind(version, take(clang_getCursorSpelling(locals[local].declaration)), std::move(value),
                    std::move(*body), statement);
    }

    std::optional<Expr> lower_assignment(CXCursor statement, const Continuation& next, const Locals& locals,
                                         unsigned depth) {
        const std::vector<CXCursor> operands = children_of(statement);
        if (operands.size() != 2) {
            return reject("an assignment requires a target and a value");
        }
        const std::optional<std::size_t> local = written_local(operands[0], locals);
        if (!local) {
            return std::nullopt;
        }
        const Type& type = locals[*local].type;
        Expr value = build_expression(operands[1], parameters, locals, 0);
        if (!std::holds_alternative<Unsupported>(value.node) && !same_modeled_value(type, value.type)) {
            return reject("assigning '" + value.type.spelling + "' to '" +
                          take(clang_getCursorSpelling(locals[*local].declaration)) + "' of type '" + type.spelling +
                          "' is a conversion that is not modeled");
        }
        return write(*local, std::move(value), statement, next, locals, depth);
    }

    // `x += e`, `x -= e`, `x *= e`, `++x`, `x++`, `--x` and `x--` as statements.
    // Each is the assignment `x = x op e` (or `x op 1`) at the local's own type,
    // which C++ guarantees exactly when that type is not promoted first; the
    // arithmetic is then modeled or refused like any other (SPEC.md 12.8).
    std::optional<Expr> lower_update(CXCursor statement, const Continuation& next, const Locals& locals,
                                     unsigned depth) {
        const CXCursorKind kind = clang_getCursorKind(statement);
        const std::vector<CXCursor> operands = children_of(statement);
        BinaryOp op = BinaryOp::Unsupported;
        if (kind == CXCursor_CompoundAssignOperator) {
            const enum CXBinaryOperatorKind written = clang_getCursorBinaryOperatorKind(statement);
            if (written == CXBinaryOperator_AddAssign) {
                op = BinaryOp::Add;
            } else if (written == CXBinaryOperator_SubAssign) {
                op = BinaryOp::Sub;
            } else if (written == CXBinaryOperator_MulAssign) {
                op = BinaryOp::Mul;
            } else {
                return reject("compound assignment '" + take(clang_getBinaryOperatorKindSpelling(written)) +
                              "' is not modeled");
            }
            if (operands.size() != 2) {
                return reject("a compound assignment requires a target and a value");
            }
        } else {
            const enum CXUnaryOperatorKind written = clang_getCursorUnaryOperatorKind(statement);
            if (written == CXUnaryOperator_PreInc || written == CXUnaryOperator_PostInc) {
                op = BinaryOp::Add;
            } else if (written == CXUnaryOperator_PreDec || written == CXUnaryOperator_PostDec) {
                op = BinaryOp::Sub;
            } else {
                return reject(
                    unmodeled_statement("operator '" + take(clang_getUnaryOperatorKindSpelling(written)) + "'"));
            }
            if (operands.size() != 1) {
                return reject("an increment or decrement requires one operand");
            }
        }

        const std::optional<std::size_t> local = written_local(operands[0], locals);
        if (!local) {
            return std::nullopt;
        }
        const Local& target = locals[*local];
        const std::string name = take(clang_getCursorSpelling(target.declaration));
        if (promoted_before_arithmetic(clang_getCursorType(target.declaration))) {
            return reject("updating '" + name + "' of type '" + target.type.spelling +
                          "' computes in 'int' after promotion and converts back, which is not modeled");
        }

        Expr current;
        current.type = target.type;
        current.location = presumed_location(clang_getCursorLocation(operands[0]));
        current.node = LocalRef{target.version, name};

        Expr amount;
        if (operands.size() == 2) {
            amount = build_expression(operands[1], parameters, locals, 0);
            if (!std::holds_alternative<Unsupported>(amount.node) && !same_modeled_value(target.type, amount.type)) {
                return reject("updating '" + name + "' of type '" + target.type.spelling + "' by '" +
                              amount.type.spelling + "' is a conversion that is not modeled");
            }
        } else {
            amount.type = target.type;
            amount.location = presumed_location(clang_getCursorLocation(statement));
            amount.node = IntLiteral{1};
        }

        Expr value;
        value.type = target.type;
        value.location = presumed_location(clang_getCursorLocation(statement));
        value.node = Binary{op, {std::move(current), std::move(amount)}};
        return write(*local, std::move(value), statement, next, locals, depth);
    }
};

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

    const std::vector<CXCursor> statements = children_of(members[body_index]);
    BodyLowering lowering{parameters, 0, {}};
    function.returned_value = lowering.lower_statements(Continuation{nullptr, &statements, 0}, {}, 0);
    if (!function.returned_value) {
        function.body_rejection = lowering.rejection.empty() ? "every path must return a value" : lowering.rejection;
    }
}

std::size_t physical_offset(CXCursor cursor) {
    unsigned offset = 0;
    clang_getFileLocation(clang_getCursorLocation(cursor), nullptr, nullptr, nullptr, &offset);
    return offset;
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

    const auto offset = physical_offset(cursor);
    return std::ranges::find(selection.offsets, offset) != selection.offsets.end();
}

CXChildVisitResult collect(CXCursor cursor, CXCursor, CXClientData data) {
    auto& collector = *static_cast<Collector*>(data);
    const CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind == CXCursor_Namespace || kind == CXCursor_UnexposedDecl || kind == CXCursor_LinkageSpec) {
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

} // namespace

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

const Function* TranslationUnit::find_at_offset(std::size_t offset) const {
    const Function* found = nullptr;
    for (const Function& function : functions) {
        if (function.analysis_offset == offset) {
            if (found != nullptr)
                return nullptr;
            found = &function;
        }
    }
    return found;
}

std::expected<TranslationUnit, std::string> parse(const ParseRequest& request) {
    CXIndex index = clang_createIndex(/*excludeDeclarationsFromPCH=*/0, /*displayDiagnostics=*/0);
    if (index == nullptr) {
        return std::unexpected("could not create a Clang index");
    }

    struct ReleaseIndex {
        CXIndex index;
        ~ReleaseIndex() {
            clang_disposeIndex(index);
        }
    } release_index{index};

    std::vector<const char*> argv;
    argv.reserve(request.arguments.size());
    for (const std::string& argument : request.arguments) {
        argv.push_back(argument.c_str());
    }

    CXTranslationUnit unit = nullptr;
    const CXErrorCode error =
        clang_parseTranslationUnit2(index, request.path.c_str(), argv.data(), static_cast<int>(argv.size()), nullptr, 0,
                                    CXTranslationUnit_None, &unit);
    if (error != CXError_Success || unit == nullptr) {
        return std::unexpected("Clang failed to parse '" + request.path + "'");
    }

    struct ReleaseUnit {
        CXTranslationUnit unit;
        ~ReleaseUnit() {
            clang_disposeTranslationUnit(unit);
        }
    } release_unit{unit};

    TranslationUnit result;

    const unsigned diagnostic_count = clang_getNumDiagnostics(unit);
    for (unsigned index_of_diagnostic = 0; index_of_diagnostic < diagnostic_count; ++index_of_diagnostic) {
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
        function.analysis_offset = physical_offset(cursor);

        const std::vector<CXCursor> parameter_cursors = parameters_of(cursor);
        for (const CXCursor& parameter : parameter_cursors) {
            function.parameters.push_back(
                Parameter{take(clang_getCursorSpelling(parameter)), convert_type(clang_getCursorType(parameter))});
        }

        extract_body(function, cursor, parameter_cursors);
        result.functions.push_back(std::move(function));
    }

    return result;
}

std::string clang_version() {
    return take(clang_getClangVersion());
}

} // namespace cppl::clangbridge
