#include "cppl/clang/bridge.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstdint>
#include <limits>
#include <ranges>
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

    // Layout is asked only of built-in integer types, which always have one.
    long long size = 0;
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
            size = clang_Type_getSizeOf(canonical);
            if (size > 0 && size <= 8) {
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
            size = clang_Type_getSizeOf(canonical);
            if (size > 0 && size <= 8) {
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
    for (auto& part : std::views::reverse(parts)) {
        if (!qualified.empty()) {
            qualified += "::";
        }
        qualified += part;
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
std::vector<Refinement> refinements_of(CXCursor declared, CXType written,
                                       const std::vector<Selection::Refinement>& known) {
    std::vector<Refinement> found;
    if (known.empty()) {
        return found;
    }

    std::vector<std::int64_t> arguments;
    const CXCursor initializer = clang_Cursor_getVarDeclInitializer(declared);
    for (const CXCursor& child : children_of(declared)) {
        if (clang_Cursor_isNull(initializer) == 0 && clang_equalCursors(child, initializer) != 0) {
            break;
        }
        const CXCursorKind kind = clang_getCursorKind(child);
        if (kind == CXCursor_TemplateRef || kind == CXCursor_TypeRef || kind == CXCursor_NamespaceRef) {
            continue;
        }
        // Index arguments stand before anything the declaration itself contains.
        if (clang_isDeclaration(kind) != 0 || clang_isStatement(kind) != 0) {
            break;
        }
        if (CXEvalResult evaluated = clang_Cursor_Evaluate(child)) {
            const bool integral = clang_EvalResult_getKind(evaluated) == CXEval_Int;
            const long long value = integral ? clang_EvalResult_getAsLongLong(evaluated) : 0;
            clang_EvalResult_dispose(evaluated);
            if (integral) {
                arguments.push_back(static_cast<std::int64_t>(value));
                continue;
            }
        }
        break;
    }

    // An alias chain is finite, and this bounds it even if Clang hands back one
    // that is not.
    for (unsigned step = 0; step < 32; ++step) {
        const CXCursor declaration = clang_getTypeDeclaration(written);
        const CXCursorKind kind = clang_getCursorKind(declaration);
        if (kind != CXCursor_TypeAliasDecl && kind != CXCursor_TypedefDecl && kind != CXCursor_TypeAliasTemplateDecl) {
            break;
        }
        const std::string name = take(clang_getCursorSpelling(declaration));
        const auto entry =
            std::ranges::find_if(known, [&](const Selection::Refinement& candidate) { return candidate.name == name; });
        if (entry == known.end()) {
            break; // an ordinary alias, which refines nothing
        }

        Refinement refinement;
        refinement.name = name;
        if (entry->index_count != 0 && arguments.size() >= entry->index_count) {
            refinement.arguments.assign(arguments.begin(),
                                        arguments.begin() + static_cast<std::ptrdiff_t>(entry->index_count));
        }
        found.push_back(std::move(refinement));

        const CXType underlying = clang_getTypedefDeclUnderlyingType(declaration);
        if (underlying.kind == CXType_Invalid || clang_equalTypes(underlying, written) != 0) {
            break;
        }
        written = underlying;
        arguments.clear(); // only the written type's own application has values here
    }
    return found;
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
    return "only if/else, while and for loops, blocks, local declarations, assignments, and return statements are "
           "modeled; found " +
           found;
}

// Names what the author wrote, not Clang's class for it.
std::string unmodeled_expression(CXCursor cursor, CXCursorKind kind) {
    switch (kind) {
        case CXCursor_UnaryOperator:
            return "operator '" + take(clang_getUnaryOperatorKindSpelling(clang_getCursorUnaryOperatorKind(cursor))) +
                   "' is not modeled";
        case CXCursor_ConditionalOperator:
            return "the conditional operator '?:' is not modeled; if/else is";
        case CXCursor_CStyleCastExpr:
        case CXCursor_CXXFunctionalCastExpr:
        case CXCursor_CXXStaticCastExpr:
        case CXCursor_CXXConstCastExpr:
        case CXCursor_CXXReinterpretCastExpr:
        case CXCursor_CXXDynamicCastExpr:
            return "an explicit conversion is not modeled";
        case CXCursor_FloatingLiteral:
            return "floating-point values are not modeled";
        case CXCursor_CXXBoolLiteralExpr:
            return "'bool' literals are not modeled";
        case CXCursor_MemberRefExpr:
            return "member access is not modeled";
        case CXCursor_ArraySubscriptExpr:
            return "subscripting is not modeled";
        case CXCursor_CXXThisExpr:
            return "'this' is not modeled";
        case CXCursor_CXXNewExpr:
        case CXCursor_CXXDeleteExpr:
            return "dynamic allocation is not modeled";
        case CXCursor_CXXThrowExpr:
            return "exceptions are not modeled";
        default:
            return "'" + take(clang_getCursorKindSpelling(kind)) + "' is not modeled";
    }
}

std::string statement_name(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_SwitchStmt:
            return "a 'switch' statement";
        case CXCursor_GotoStmt:
        case CXCursor_IndirectGotoStmt:
            return "a 'goto' statement";
        case CXCursor_LabelStmt:
            return "a label";
        case CXCursor_CXXTryStmt:
            return "a 'try' block (exceptions are not modeled)";
        case CXCursor_CXXThrowExpr:
            return "a 'throw' (exceptions are not modeled)";
        case CXCursor_CallExpr:
            return "a call whose value is discarded (effects are not modeled)";
        case CXCursor_NullStmt:
            return "an empty statement";
        case CXCursor_GCCAsmStmt:
        case CXCursor_MSAsmStmt:
            return "inline assembly";
        default:
            return "'" + take(clang_getCursorKindSpelling(kind)) + "'";
    }
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
        } else if (op == CXBinaryOperator_LAnd) {
            mapped = BinaryOp::And;
        } else if (op == CXBinaryOperator_LOr) {
            mapped = BinaryOp::Or;
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

    return unsupported_expression(cursor, unmodeled_expression(cursor, kind));
}

std::vector<CXCursor> parameters_of(CXCursor cursor) {
    std::vector<CXCursor> parameters;
    // Negative for a cursor that is not a function; there are then no arguments.
    const int count = clang_Cursor_getNumArguments(cursor);
    if (count <= 0) {
        return parameters;
    }
    parameters.reserve(static_cast<std::size_t>(count));
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
    if (const auto* loop = std::get_if<Loop>(&expression.node)) {
        return return_paths(loop->operands.back());
    }
    return 1;
}

std::size_t file_offset(CXSourceLocation location) {
    unsigned offset = 0;
    clang_getFileLocation(location, nullptr, nullptr, nullptr, &offset);
    return static_cast<std::size_t>(offset);
}

// The parts of a `for` header. libclang omits an empty part instead of marking
// it, so each part is placed by where it starts relative to the header's two
// top-level semicolons.
struct ForParts {
    std::optional<CXCursor> initialization;
    std::optional<CXCursor> condition;
    std::optional<CXCursor> increment;
    CXCursor body = clang_getNullCursor();
};

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

// Marks each local in `locals` that the statement or expression writes by
// assignment, compound assignment, increment or decrement. Any other way of
// writing a local is refused when the body is lowered, and a local this misses
// is caught at the end of every iteration, so the scan only has to be complete
// for the writes the lowering accepts.
struct WriteScan {
    const Locals* locals;
    std::vector<bool>* written;
};

void mark_write(CXCursor cursor, const WriteScan& scan) {
    const CXCursorKind kind = clang_getCursorKind(cursor);
    const bool assigns =
        (kind == CXCursor_BinaryOperator && clang_getCursorBinaryOperatorKind(cursor) == CXBinaryOperator_Assign) ||
        kind == CXCursor_CompoundAssignOperator;
    bool updates = false;
    if (kind == CXCursor_UnaryOperator) {
        const enum CXUnaryOperatorKind op = clang_getCursorUnaryOperatorKind(cursor);
        updates = op == CXUnaryOperator_PreInc || op == CXUnaryOperator_PostInc || op == CXUnaryOperator_PreDec ||
                  op == CXUnaryOperator_PostDec;
    }
    if (!assigns && !updates) {
        return;
    }
    const std::vector<CXCursor> operands = children_of(cursor);
    if (operands.empty()) {
        return;
    }
    CXCursor target = operands[0];
    while (clang_getCursorKind(target) == CXCursor_ParenExpr) {
        const std::vector<CXCursor> inner = children_of(target);
        if (inner.size() != 1) {
            return;
        }
        target = inner[0];
    }
    if (clang_getCursorKind(target) != CXCursor_DeclRefExpr) {
        return;
    }
    if (const std::optional<std::size_t> local = find_local(*scan.locals, clang_getCursorReferenced(target))) {
        (*scan.written)[*local] = true;
    }
}

void mark_writes(CXCursor root, const Locals& locals, std::vector<bool>& written) {
    WriteScan scan{&locals, &written};
    mark_write(root, scan);
    clang_visitChildren(
        root,
        [](CXCursor child, CXCursor, CXClientData data) {
            mark_write(child, *static_cast<const WriteScan*>(data));
            return CXChildVisit_Recurse;
        },
        &scan);
}

// Whether control leaves the function rather than reaching what follows. It
// decides reachability only; what each statement means is decided by the
// lowering below.
bool terminates(CXCursor statement, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return false;
    }
    const CXCursorKind kind = clang_getCursorKind(statement);
    if (kind == CXCursor_ReturnStmt || kind == CXCursor_BreakStmt || kind == CXCursor_ContinueStmt) {
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
struct LoopFrame;
struct LoopHeader;

struct Continuation {
    const Continuation* outer = nullptr;
    const std::vector<CXCursor>* statements = nullptr;
    std::size_t index = 0;

    // In place of statements: the end of one iteration of a loop, before or
    // after its increment, or a `for` loop whose initialization is done.
    const LoopFrame* iteration = nullptr;
    bool after_increment = false;
    const LoopHeader* header = nullptr;
};

// A loop about to be entered.
struct LoopHeader {
    CXCursor statement = clang_getNullCursor();
    CXCursor condition = clang_getNullCursor();
    CXCursor body = clang_getNullCursor();
    std::optional<CXCursor> increment;
    const Continuation* exit = nullptr; // what follows the loop
};

// A loop whose body is being lowered.
struct LoopFrame {
    std::uint32_t id = 0;
    CXCursor statement = clang_getNullCursor();
    Locals head;                      // the locals at the head, each carried one at its head version
    std::vector<std::size_t> carried; // positions in `head` that the loop writes
    std::optional<CXCursor> increment;
    const Continuation* exit = nullptr;
    std::size_t frames_outside = 0; // the enclosing loops, for a `break` into what follows
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
    Type result_type;
    std::string invariant_prefix; // the projector's generated invariant declarations
    const std::vector<Selection::Refinement>* refinements = nullptr;
    std::uint32_t next_version = 0;
    std::uint32_t next_loop = 0;
    std::vector<const LoopFrame*> frames;
    std::vector<std::string> consumed_invariants;
    std::string rejection;

    std::nullopt_t reject(std::string reason) {
        if (rejection.empty()) {
            rejection = std::move(reason);
        }
        return std::nullopt;
    }

    Expr bind(std::uint32_t version, std::string name, Expr value, Expr body, CXCursor at, Type declared = {}) {
        Expr expr;
        expr.type = body.type;
        expr.location = presumed_location(clang_getCursorLocation(at));
        expr.node = LocalVersion{version, std::move(name), {std::move(value), std::move(body)}, std::move(declared)};
        return expr;
    }

    std::optional<Expr> lower_statements(const Continuation& from, const Locals& locals, unsigned depth) {
        // Each statement lowers the rest of the body inside itself, so this
        // bounds the statements on one path as well as their nesting.
        if (depth > kMaxExpressionDepth) {
            return reject("more than " + std::to_string(kMaxExpressionDepth) +
                          " nested or consecutive statements on one path are not modeled");
        }
        if (from.header != nullptr) {
            return lower_loop(*from.header, locals, depth + 1);
        }
        if (from.iteration != nullptr) {
            return end_iteration(*from.iteration, from.after_increment, locals, depth + 1);
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
        if (kind == CXCursor_WhileStmt) {
            if (parts.size() != 2 || clang_isExpression(clang_getCursorKind(parts[0])) == 0) {
                return reject("a while loop whose condition declares a variable is not modeled");
            }
            const LoopHeader header{statement, parts[0], parts[1], std::nullopt, &next};
            return lower_loop(header, locals, depth);
        }
        if (kind == CXCursor_ForStmt) {
            return lower_for(statement, next, locals, depth);
        }
        if (kind == CXCursor_BreakStmt) {
            return lower_break(locals, depth);
        }
        if (kind == CXCursor_ContinueStmt) {
            if (frames.empty()) {
                return reject("'continue' outside a modeled loop");
            }
            return end_iteration(*frames.back(), false, locals, depth);
        }
        if (kind == CXCursor_DoStmt) {
            return reject("do-while loops are not modeled");
        }
        if (kind == CXCursor_CXXForRangeStmt) {
            return reject("range-based for loops are not modeled");
        }
        return reject(unmodeled_statement(statement_name(kind)));
    }

    std::optional<Expr> lower_for(CXCursor statement, const Continuation& next, const Locals& locals, unsigned depth) {
        const std::optional<ForParts> parts = for_parts(statement);
        if (!parts) {
            return reject("the parts of this for loop could not be resolved");
        }
        if (!parts->condition) {
            return reject("a for loop without a condition is not modeled");
        }
        if (clang_isExpression(clang_getCursorKind(*parts->condition)) == 0) {
            return reject("a for loop whose condition declares a variable is not modeled");
        }
        const LoopHeader header{statement, *parts->condition, parts->body, parts->increment, &next};
        if (!parts->initialization) {
            return lower_loop(header, locals, depth);
        }
        // The initialization runs once, before the loop, with the loop as what
        // follows it.
        Continuation entered;
        entered.header = &header;
        return lower_statement(*parts->initialization, entered, locals, depth);
    }

    // The generated declaration a loop invariant was projected into, if the
    // statement is one.
    [[nodiscard]] std::optional<CXCursor> invariant_marker(CXCursor statement) const {
        if (invariant_prefix.empty() || clang_getCursorKind(statement) != CXCursor_DeclStmt) {
            return std::nullopt;
        }
        const std::vector<CXCursor> declared = children_of(statement);
        if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl ||
            !take(clang_getCursorSpelling(declared[0])).starts_with(invariant_prefix)) {
            return std::nullopt;
        }
        return declared[0];
    }

    Expr local_read(const Local& local, CXCursor at) {
        Expr read;
        read.type = local.type;
        read.location = presumed_location(clang_getCursorLocation(at));
        read.node = LocalRef{local.version, take(clang_getCursorSpelling(local.declaration))};
        return read;
    }

    // A loop, as its entry, its head, one iteration, and what follows it
    // (SPEC.md 24). Every local the loop writes is carried: from the head on it
    // denotes a fresh version, of which only the invariants and the condition
    // are known. A local the loop does not write keeps the version it had.
    std::optional<Expr> lower_loop(const LoopHeader& header, const Locals& locals, unsigned depth) {
        if (depth > kMaxExpressionDepth) {
            return reject("more than " + std::to_string(kMaxExpressionDepth) +
                          " nested or consecutive statements on one path are not modeled");
        }
        std::vector<CXCursor> statements;
        if (clang_getCursorKind(header.body) == CXCursor_CompoundStmt) {
            statements = children_of(header.body);
        } else {
            statements.push_back(header.body);
        }
        std::vector<CXCursor> markers;
        std::size_t first = 0;
        while (first < statements.size()) {
            const std::optional<CXCursor> marker = invariant_marker(statements[first]);
            if (!marker) {
                break;
            }
            markers.push_back(*marker);
            ++first;
        }

        LoopFrame frame;
        frame.id = next_loop++;
        frame.statement = header.statement;
        frame.head = locals;
        frame.increment = header.increment;
        frame.exit = header.exit;
        frame.frames_outside = frames.size();
        std::vector<bool> written(locals.size(), false);
        mark_writes(header.condition, locals, written);
        if (header.increment) {
            mark_writes(*header.increment, locals, written);
        }
        mark_writes(header.body, locals, written);
        for (std::size_t index = 0; index < locals.size(); ++index) {
            if (written[index]) {
                frame.carried.push_back(index);
                frame.head[index].version = next_version++;
            }
        }

        std::vector<Expr> invariants;
        for (const CXCursor marker : markers) {
            const CXCursor initializer = clang_Cursor_getVarDeclInitializer(marker);
            if (clang_Cursor_isNull(initializer) != 0) {
                return reject("a loop invariant was not resolved");
            }
            Expr invariant = build_expression(initializer, parameters, frame.head, 0);
            if (!std::holds_alternative<Unsupported>(invariant.node) && invariant.type.kind != TypeKind::Bool) {
                return reject("a loop invariant must be a condition");
            }
            invariants.push_back(std::move(invariant));
            consumed_invariants.push_back(take(clang_getCursorSpelling(marker)));
        }
        Expr condition = build_expression(header.condition, parameters, frame.head, 0);

        frames.push_back(&frame);
        const std::vector<CXCursor> rest(statements.begin() + static_cast<std::ptrdiff_t>(first), statements.end());
        Continuation iteration;
        iteration.iteration = &frame;
        std::optional<Expr> once = lower_statements(Continuation{&iteration, &rest, 0}, frame.head, depth + 1);
        frames.pop_back();
        if (!once) {
            return std::nullopt;
        }
        std::optional<Expr> after = lower_statements(*header.exit, frame.head, depth + 1);
        if (!after) {
            return std::nullopt;
        }
        if (return_paths(*once) + return_paths(*after) > kMaxReturnPaths) {
            return reject("more than " + std::to_string(kMaxReturnPaths) + " return paths are not modeled");
        }

        const source::SourceLocation location = presumed_location(clang_getCursorLocation(header.statement));
        Expr head;
        head.type = result_type;
        head.location = location;
        head.node = Conditional{{std::move(condition), std::move(*once), std::move(*after)}};

        Loop loop;
        loop.loop = frame.id;
        for (const std::size_t index : frame.carried) {
            loop.heads.push_back(frame.head[index].version);
            loop.names.push_back(take(clang_getCursorSpelling(locals[index].declaration)));
            loop.operands.push_back(local_read(locals[index], header.statement));
        }
        loop.invariants = static_cast<std::uint32_t>(invariants.size());
        for (Expr& invariant : invariants) {
            loop.operands.push_back(std::move(invariant));
        }
        loop.operands.push_back(std::move(head));

        Expr lowered;
        lowered.type = result_type;
        lowered.location = location;
        lowered.node = std::move(loop);
        return lowered;
    }

    // The end of an iteration: the increment, then the next iteration with
    // each carried local at the version it holds here. A local the loop does
    // not carry must still hold its head version, or the scan that decided
    // what the loop carries missed a write.
    std::optional<Expr> end_iteration(const LoopFrame& frame, bool after_increment, const Locals& locals,
                                      unsigned depth) {
        if (frame.increment && !after_increment) {
            Continuation incremented;
            incremented.iteration = &frame;
            incremented.after_increment = true;
            return lower_statement(*frame.increment, incremented, locals, depth + 1);
        }
        if (locals.size() < frame.head.size()) {
            return reject("a loop's locals went out of step with its head");
        }
        Iterate next;
        next.loop = frame.id;
        for (std::size_t index = 0; index < frame.head.size(); ++index) {
            if (clang_equalCursors(locals[index].declaration, frame.head[index].declaration) == 0) {
                return reject("a loop's locals went out of step with its head");
            }
            const bool carried = std::ranges::find(frame.carried, index) != frame.carried.end();
            if (!carried && locals[index].version != frame.head[index].version) {
                return reject("'" + take(clang_getCursorSpelling(locals[index].declaration)) +
                              "' is written inside a loop in a way this implementation does not track");
            }
            if (carried) {
                next.operands.push_back(local_read(locals[index], frame.statement));
            }
        }
        Expr iterated;
        iterated.type = result_type;
        iterated.location = presumed_location(clang_getCursorLocation(frame.statement));
        iterated.node = std::move(next);
        return iterated;
    }

    // `break` continues with what follows the innermost loop, under the
    // versions current here, and outside that loop.
    std::optional<Expr> lower_break(const Locals& locals, unsigned depth) {
        if (frames.empty()) {
            return reject("'break' outside a modeled loop");
        }
        const LoopFrame& frame = *frames.back();
        const std::vector<const LoopFrame*> inside = frames;
        frames.resize(frame.frames_outside);
        std::optional<Expr> after = lower_statements(*frame.exit, locals, depth + 1);
        frames = inside;
        return after;
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
        // An invariant the loop lowering did not take is never read as a
        // statement of the body: that would drop it without a word.
        if (!invariant_prefix.empty() && name.starts_with(invariant_prefix)) {
            return reject("a loop invariant is attached only to a while or for loop whose body is a block");
        }
        const enum CX_StorageClass storage = clang_Cursor_getStorageClass(declaration);
        if (storage != CX_SC_None && storage != CX_SC_Auto) {
            return reject("local '" + name + "' does not have automatic storage");
        }
        if (clang_getCursorTLSKind(declaration) != CXTLS_None) {
            return reject("thread-local '" + name + "' is not modeled");
        }
        Type type = convert_type(clang_getCursorType(declaration));
        if (type.kind == TypeKind::Unsupported) {
            return reject("local '" + name + "' has type '" + type.spelling + "', which is not modeled");
        }
        if (refinements != nullptr) {
            type.refinements = refinements_of(declaration, clang_getCursorType(declaration), *refinements);
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
        return bind(version, name, std::move(value), std::move(*body), declaration, type);
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

void extract_body(Function& function, CXCursor cursor, const std::vector<CXCursor>& parameters,
                  const std::string& invariant_prefix, const std::vector<Selection::Refinement>& refinements) {
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
    BodyLowering lowering{parameters, function.result, invariant_prefix, &refinements, 0, 0, {}, {}, {}};
    function.returned_value = lowering.lower_statements(Continuation{nullptr, &statements, 0}, {}, 0);
    if (!function.returned_value) {
        function.body_rejection = lowering.rejection.empty() ? "every path must return a value" : lowering.rejection;
    }
    function.loop_invariants = std::move(lowering.consumed_invariants);
}

std::size_t physical_offset(CXCursor cursor) {
    unsigned offset = 0;
    clang_getFileLocation(clang_getCursorLocation(cursor), nullptr, nullptr, nullptr, &offset);
    return static_cast<std::size_t>(offset);
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

// The schema describes only syntax emitted by the projector. Every C++ leaf,
// parameter type and declaration reference is resolved independently by Clang.
std::expected<Expr, std::string> build_formal(CXCursor cursor, const source::ProjectionShape& shape,
                                              const std::vector<CXCursor>& parameters, unsigned depth) {
    using Kind = source::ProjectionKind;
    if (depth > kMaxExpressionDepth)
        return std::unexpected("formal proposition nests too deeply");
    if (shape.kind == Kind::Expression) {
        if (!shape.children.empty())
            return std::unexpected("malformed expression projection");
        return build_expression(cursor, parameters, {}, 0);
    }
    while (clang_getCursorKind(cursor) == CXCursor_UnexposedExpr || clang_getCursorKind(cursor) == CXCursor_ParenExpr) {
        const auto children = children_of(cursor);
        if (children.size() != 1)
            return std::unexpected("malformed formal expression wrapper");
        cursor = children[0];
    }
    Expr result;
    result.type.kind = TypeKind::Proposition;
    result.type.spelling = "Prop";
    result.location = presumed_location(clang_getCursorLocation(cursor));

    if (shape.kind == Kind::Equality) {
        if (!shape.children.empty() || clang_getCursorKind(cursor) != CXCursor_CallExpr)
            return std::unexpected("malformed equality probe");
        const auto method = clang_getCursorReferenced(cursor);
        const auto formals = parameters_of(method);
        if (clang_getCursorKind(method) != CXCursor_CXXMethod || formals.size() != 2 ||
            clang_Cursor_getNumArguments(cursor) != 3)
            return std::unexpected("malformed equality operands");
        const auto first = clang_getCanonicalType(clang_getCursorType(formals[0]));
        const auto second = clang_getCanonicalType(clang_getCursorType(formals[1]));
        if (clang_equalTypes(first, second) == 0)
            return std::unexpected("equality operand types differ");
        FormalEquality equality{convert_type(first), {}};
        // The first operator() argument is the closure object.
        for (unsigned index = 1; index < 3; ++index)
            equality.operands.push_back(build_expression(clang_Cursor_getArgument(cursor, index), parameters, {}, 0));
        result.node = std::move(equality);
        return result;
    }

    if (clang_getCursorKind(cursor) != CXCursor_LambdaExpr)
        return std::unexpected("formal scope is not a projected C++ lambda");
    std::vector<CXCursor> binders;
    std::vector<CXCursor> bodies;
    for (const auto child : children_of(cursor)) {
        if (clang_getCursorKind(child) == CXCursor_ParmDecl)
            binders.push_back(child);
        if (clang_getCursorKind(child) == CXCursor_CompoundStmt)
            bodies.push_back(child);
    }
    if (bodies.size() != 1)
        return std::unexpected("formal scope requires one body");
    const auto statements = children_of(bodies[0]);
    if (shape.kind == Kind::Universal) {
        if (binders.empty() || shape.children.size() != 1 || statements.size() != 1 ||
            clang_getCursorKind(statements[0]) != CXCursor_ReturnStmt)
            return std::unexpected("forall requires binders and one proposition");
        const auto values = children_of(statements[0]);
        if (values.size() != 1)
            return std::unexpected("forall has no proposition");
        auto scope = parameters;
        scope.insert(scope.end(), binders.begin(), binders.end());
        auto body = build_formal(values[0], shape.children[0], scope, depth + 1);
        if (!body)
            return body;
        Universal quantified;
        for (const auto binder : binders)
            quantified.binders.push_back(convert_type(clang_getCursorType(binder)));
        quantified.body.push_back(std::move(*body));
        result.node = std::move(quantified);
        return result;
    }
    if (shape.kind == Kind::Implication || shape.kind == Kind::Conjunction || shape.kind == Kind::Disjunction ||
        shape.kind == Kind::Equivalence) {
        if (!binders.empty() || shape.children.size() != 2 || statements.size() != 2)
            return std::unexpected("logical connective requires exactly two propositions");
        std::vector<Expr> operands;
        for (std::size_t index = 0; index < 2; ++index) {
            auto operand = build_formal(statements[index], shape.children[index], parameters, depth + 1);
            if (!operand)
                return operand;
            operands.push_back(std::move(*operand));
        }
        if (shape.kind == Kind::Implication) {
            result.node = Implication{std::move(operands)};
        } else {
            const auto kind = shape.kind == Kind::Conjunction   ? Connective::Kind::Conjunction
                              : shape.kind == Kind::Disjunction ? Connective::Kind::Disjunction
                                                                : Connective::Kind::Equivalence;
            result.node = Connective{kind, std::move(operands)};
        }
        return result;
    }
    return std::unexpected("unknown formal projection form");
}

void extract_formal(Function& function, CXCursor cursor, const std::vector<CXCursor>& parameters,
                    const source::ProjectionShape& shape) {
    function.has_body = true;
    function.body_rejection = "malformed formal proposition probe";
    for (const auto child : children_of(cursor)) {
        if (clang_getCursorKind(child) != CXCursor_CompoundStmt)
            continue;
        const auto statements = children_of(child);
        if (statements.size() != 1 || clang_getCursorKind(statements[0]) != CXCursor_ReturnStmt)
            return;
        const auto values = children_of(statements[0]);
        if (values.size() != 1)
            return;
        auto expression = build_formal(values[0], shape, parameters, 0);
        if (!expression) {
            function.body_rejection = expression.error();
            return;
        }
        function.returned_value = std::move(*expression);
        function.body_rejection.reset();
        return;
    }
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

    // A rejected unit is never verified, and libclang's layout queries can
    // crash on the error types of its recovery expressions.
    if (result.has_errors) {
        return result;
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

        // A refinement on a parameter or a result is verification-level identity
        // Clang canonicalizes away, so it is recovered from the written type here
        // (SPEC.md 17.3): a refined parameter carries its predicate into the body,
        // and a refined result states one at every return.
        function.result.refinements =
            refinements_of(cursor, clang_getCursorResultType(cursor), request.selection.refinements);

        const std::vector<CXCursor> parameter_cursors = parameters_of(cursor);
        for (const CXCursor& parameter : parameter_cursors) {
            Type parameter_type = convert_type(clang_getCursorType(parameter));
            parameter_type.refinements =
                refinements_of(parameter, clang_getCursorType(parameter), request.selection.refinements);
            function.parameters.push_back(
                Parameter{take(clang_getCursorSpelling(parameter)), std::move(parameter_type)});
        }

        // The projector's invariant declarations share the generated prefix,
        // which no ordinary declaration may use.
        const auto probe = std::ranges::find_if(request.selection.proposition_probes,
                                                [&](const auto& selected) { return selected.name == function.name; });
        if (probe != request.selection.proposition_probes.end()) {
            extract_formal(function, cursor, parameter_cursors, probe->shape);
        } else {
            extract_body(function, cursor, parameter_cursors,
                         request.selection.specification_prefix.empty()
                             ? std::string()
                             : request.selection.specification_prefix + "invariant_",
                         request.selection.refinements);
        }
        result.functions.push_back(std::move(function));
    }

    return result;
}

std::string clang_version() {
    return take(clang_getClangVersion());
}

} // namespace cppl::clangbridge
