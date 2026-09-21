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

source::RepresentationKind library_kind(CXCursor declaration) {
    using K = source::RepresentationKind;
    CXCursor primary = clang_getSpecializedCursorTemplate(declaration);
    if (clang_Cursor_isNull(primary))
        return K::Record;
    primary = clang_getCanonicalCursor(primary);
    CXCursor parent = clang_getCursorSemanticParent(primary);
    while (clang_getCursorKind(parent) == CXCursor_Namespace && clang_Cursor_isInlineNamespace(parent))
        parent = clang_getCursorSemanticParent(parent);
    if (clang_getCursorKind(parent) != CXCursor_Namespace || take(clang_getCursorSpelling(parent)) != "std" ||
        clang_getCursorKind(clang_getCursorSemanticParent(parent)) != CXCursor_TranslationUnit)
        return K::Record;
    // This is declaration identity in the canonical standard namespace, not a
    // spelling of a source type. Alias expansion and substitution precede it.
    const std::string name = take(clang_getCursorSpelling(primary));
    if (name == "variant")
        return K::Variant;
    if (name == "optional")
        return K::Optional;
    if (name == "expected")
        return K::Expected;
    if (name == "pair")
        return K::Pair;
    if (name == "tuple")
        return K::Tuple;
    if (name == "array")
        return K::StdArray;
    return K::Record;
}

// Whether a reference type is read through to its referent.
//
// A reference is not a value: reading one is an access to another object that
// other code may write. Treating `T&` as `T` everywhere would let a contract be
// proven about a parameter whose value can change under it (AGENTS.md 11), so
// the referent is read only where the caller has established that the subject's
// logical value is the one being reasoned about.
enum class ReferenceModel : std::uint8_t {
    Opaque,
    Referent,
};

Type convert_type(CXType type, unsigned depth = 0, ReferenceModel references = ReferenceModel::Opaque) {
    CXType canonical = clang_getCanonicalType(type);
    if (canonical.kind == CXType_LValueReference || canonical.kind == CXType_RValueReference) {
        if (references != ReferenceModel::Referent) {
            Type reference;
            reference.spelling = take(clang_getTypeSpelling(canonical));
            return reference;
        }
        canonical = clang_getCanonicalType(clang_getPointeeType(canonical));
    }

    Type converted;
    converted.spelling = take(clang_getTypeSpelling(canonical));
    if (depth > 32)
        return converted;

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
        case CXType_Void:
            converted.kind = TypeKind::Void;
            break;
        case CXType_Pointer: {
            converted.kind = TypeKind::Value;
            converted.representation.identity = "pointer:" + converted.spelling;
            converted.representation.name = converted.spelling;
            converted.representation.kind = source::RepresentationKind::Pointer;
            Type state;
            state.kind = TypeKind::Bool;
            state.spelling = "bool";
            converted.projections.push_back(state);
            break;
        }
        case CXType_ConstantArray:
        case CXType_Record: {
            using K = source::RepresentationKind;
            const bool array = canonical.kind == CXType_ConstantArray;
            const CXCursor declaration = clang_getTypeDeclaration(canonical);
            const CXCursor definition = clang_getCursorDefinition(declaration);
            converted.kind = TypeKind::Value;
            auto& model = converted.representation;
            model.name = converted.spelling;
            model.identity = array ? "array:" + converted.spelling : take(clang_getCursorUSR(declaration));
            model.kind = array ? K::Array : library_kind(declaration);
            if (model.identity.empty()) {
                converted.kind = TypeKind::Unsupported;
                break;
            }
            if (!array && model.kind == K::Record && clang_Cursor_isNull(definition)) {
                model.rejection = "proof decomposition unavailable for incomplete type";
                break;
            }
            const auto component = [&](CXType child, std::string name, CXCursor origin, bool accessible = true) {
                Type resolved = convert_type(child, depth + 1);
                if (resolved.kind == TypeKind::Unsupported) {
                    model.rejection = "component '" + name + "' has an unmodeled type '" + resolved.spelling + "'";
                    return;
                }
                converted.projections.push_back(std::move(resolved));
                model.components.push_back(
                    {std::move(name), presumed_location(clang_getCursorLocation(origin)), accessible});
            };
            if (array || model.kind == K::StdArray) {
                long long count = array ? clang_getArraySize(canonical) : -1;
                CXType element =
                    array ? clang_getArrayElementType(canonical) : clang_Type_getTemplateArgumentAsType(canonical, 0);
                if (!array && clang_Cursor_getTemplateArgumentKind(declaration, 1) == CXTemplateArgumentKind_Integral)
                    count = clang_Cursor_getTemplateArgumentValue(declaration, 1);
                if (count < 0 || count > 256) {
                    model.rejection = "array extent is unavailable or exceeds the proof resource limit";
                    break;
                }
                for (long long i = 0; i < count; ++i)
                    component(element, std::to_string(i), declaration);
            } else if (model.kind != K::Record) {
                const int count = clang_Type_getNumTemplateArguments(canonical);
                if (count < 0 || count > 64) {
                    model.rejection = "template arguments are unresolved or exceed the proof resource limit";
                    break;
                }
                if (model.kind == K::Variant || model.kind == K::Optional || model.kind == K::Expected) {
                    Type tag;
                    tag.kind = model.kind == K::Variant ? TypeKind::Int : TypeKind::Bool;
                    tag.width = 64;
                    tag.is_signed = false;
                    tag.spelling = model.kind == K::Variant ? "unsigned long long" : "bool";
                    converted.projections.push_back(tag);
                }
                for (int i = 0; i < count; ++i) {
                    CXType argument = clang_Type_getTemplateArgumentAsType(canonical, static_cast<unsigned>(i));
                    if (model.kind == K::Expected && i == 0 && argument.kind == CXType_Void) {
                        Type empty;
                        empty.kind = TypeKind::Value;
                        empty.spelling = "void";
                        empty.representation.identity = "unit";
                        converted.projections.push_back(empty);
                        model.components.push_back({"value", {}, true});
                    } else {
                        component(argument, model.kind == K::Pair ? (i == 0 ? "first" : "second") : std::to_string(i),
                                  declaration);
                    }
                }
            } else {
                if (clang_getCursorKind(definition) == CXCursor_UnionDecl) {
                    model.rejection = "a union requires an independently justified active-member model";
                    break;
                }
                for (const auto& child : children_of(definition)) {
                    if (clang_getCursorKind(child) == CXCursor_FieldDecl)
                        component(clang_getCursorType(child), take(clang_getCursorSpelling(child)), child,
                                  clang_getCXXAccessSpecifier(child) == CX_CXXPublic);
                    if (clang_getCursorKind(child) == CXCursor_CXXBaseSpecifier)
                        model.rejection = "base subobject decomposition requires an explicit accessible projection";
                }
            }
            break;
        }
        case CXType_Enum: {
            const CXCursor declaration = clang_getTypeDeclaration(canonical);
            const CXCursor definition = clang_getCursorDefinition(declaration);
            if (clang_EnumDecl_isScoped(declaration) == 0 || clang_Cursor_isNull(definition) != 0)
                break;
            const Type underlying = convert_type(clang_getEnumDeclIntegerType(declaration));
            // Bool-backed and wide enums remain outside this initial model.
            if (underlying.kind != TypeKind::Int)
                break;
            std::vector<Enumerator> enumerators;
            for (const CXCursor& child : children_of(definition)) {
                if (clang_getCursorKind(child) != CXCursor_EnumConstantDecl)
                    continue;
                enumerators.push_back(Enumerator{take(clang_getCursorSpelling(child)),
                                                 static_cast<std::int64_t>(clang_getEnumConstantDeclValue(child))});
            }
            converted.kind = underlying.kind;
            converted.width = underlying.width;
            converted.is_signed = underlying.is_signed;
            converted.representation.identity = take(clang_getCursorUSR(declaration));
            converted.representation.name = take(clang_getTypeSpelling(canonical));
            converted.representation.enumerators = std::move(enumerators);
            converted.representation.kind = source::RepresentationKind::ScopedEnum;
            break;
        }
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
std::size_t physical_offset(CXCursor cursor);

std::vector<std::int64_t> refinement_arguments(CXCursor declared) {
    std::vector<std::int64_t> arguments;
    const CXCursor initializer = clang_Cursor_getVarDeclInitializer(declared);
    for (const CXCursor child : children_of(declared)) {
        if (!clang_Cursor_isNull(initializer) && clang_equalCursors(child, initializer))
            break;
        const auto kind = clang_getCursorKind(child);
        if (kind == CXCursor_TemplateRef || kind == CXCursor_TypeRef || kind == CXCursor_NamespaceRef)
            continue;
        if (clang_isDeclaration(kind) || clang_isStatement(kind))
            break;
        if (CXEvalResult evaluated = clang_Cursor_Evaluate(child)) {
            const bool integral = clang_EvalResult_getKind(evaluated) == CXEval_Int;
            const auto value = integral ? clang_EvalResult_getAsLongLong(evaluated) : 0;
            clang_EvalResult_dispose(evaluated);
            if (integral) {
                arguments.push_back(static_cast<std::int64_t>(value));
                continue;
            }
        }
        break;
    }
    return arguments;
}

std::expected<std::vector<Refinement>, std::string> refinements_of(CXCursor declared, CXType written,
                                                                   const std::vector<Selection::Refinement>& known) {
    std::vector<Refinement> found;
    if (known.empty())
        return found;
    auto arguments = refinement_arguments(declared);
    std::vector<CXCursor> visited;
    for (unsigned step = 0; step < kMaxExpressionDepth; ++step) {
        CXCursor declaration = clang_getTypeDeclaration(written);
        if (clang_getCursorKind(declaration) == CXCursor_TypeAliasTemplateDecl) {
            const auto children = children_of(declaration);
            const auto alias = std::ranges::find_if(
                children, [](CXCursor child) { return clang_getCursorKind(child) == CXCursor_TypeAliasDecl; });
            if (alias == children.end())
                return std::unexpected("refinement alias template has no resolved alias declaration");
            declaration = *alias;
        }
        const CXCursorKind kind = clang_getCursorKind(declaration);
        if (kind != CXCursor_TypeAliasDecl && kind != CXCursor_TypedefDecl && kind != CXCursor_TypeAliasTemplateDecl)
            return found;
        if (std::ranges::any_of(visited, [&](CXCursor previous) { return clang_equalCursors(previous, declaration); }))
            return std::unexpected("cyclic refinement alias metadata");
        visited.push_back(declaration);
        // The projector records the generated alias's physical identity. Source
        // spelling and presumed #line locations cannot identify a refinement.
        const auto entry = std::ranges::find(known, physical_offset(declaration), &Selection::Refinement::alias_offset);
        if (entry != known.end()) {
            if (entry->index_count != arguments.size())
                return std::unexpected("refinement '" + entry->name + "' has unresolved index arguments");
            found.push_back(Refinement{entry->name, arguments, entry->probe});
        }
        const CXType underlying = clang_getTypedefDeclUnderlyingType(declaration);
        if (underlying.kind == CXType_Invalid) {
            if (kind == CXCursor_TypeAliasTemplateDecl)
                return std::unexpected("dependent refinement alias substitution is not resolved by the Clang bridge");
            return found;
        }
        written = underlying;
        // An ordinary alias may name an indexed refinement. Read that alias's
        // resolved application, not the initializer or a previous alias's indices.
        arguments = refinement_arguments(declaration);
    }
    return std::unexpected("refinement alias chain exceeds the analysis limit");
}

// A local's identity is the declaration Clang resolved; its current logical
// version is what a read of it denotes. Shadowing needs no rule of its own,
// because an inner declaration is a different declaration.
struct Local {
    CXCursor declaration;
    std::uint32_t version = 0;
    Type type;
    std::optional<std::size_t> referent = std::nullopt;
    bool external = false; // may alias another reference parameter
};

using Locals = std::vector<Local>;

// Preserve pointee sugar while following aliases to a reference. Canonicalizing
// first would discard the refinement attached to that pointee.
CXType reference_value_type(CXType written) {
    for (unsigned depth = 0; depth < kMaxExpressionDepth; ++depth) {
        if (written.kind == CXType_LValueReference || written.kind == CXType_RValueReference)
            return clang_getPointeeType(written);
        const auto declaration = clang_getTypeDeclaration(written);
        const auto underlying = clang_getTypedefDeclUnderlyingType(declaration);
        if (underlying.kind == CXType_Invalid)
            break;
        written = underlying;
    }
    return CXType{CXType_Invalid, {nullptr, nullptr}};
}

source::ParameterPassing passing_of(CXType written) {
    const auto canonical = clang_getCanonicalType(written);
    if (canonical.kind != CXType_LValueReference && canonical.kind != CXType_RValueReference)
        return source::ParameterPassing::Value;
    if (clang_isConstQualifiedType(clang_getPointeeType(canonical)))
        return source::ParameterPassing::ConstReference;
    return canonical.kind == CXType_RValueReference ? source::ParameterPassing::RvalueReference
                                                    : source::ParameterPassing::MutableReference;
}

std::optional<std::size_t> find_binding(const Locals& locals, CXCursor declaration) {
    for (std::size_t index = locals.size(); index > 0; --index) {
        if (clang_equalCursors(locals[index - 1].declaration, declaration) != 0) {
            return index - 1;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> find_local(const Locals& locals, CXCursor declaration) {
    const auto binding = find_binding(locals, declaration);
    return binding ? std::optional{locals[*binding].referent.value_or(*binding)} : std::nullopt;
}

// Whether two Clang types denote the same modeled value. Qualifiers are not
// part of a value, so a read of a `const` local is the value it holds; two
// spellings that Clang laid out identically are the same machine integer. A
// type C++L does not model is never "the same" as anything.
bool same_modeled_value(const Type& outer, const Type& inner) {
    return outer.kind != TypeKind::Unsupported && outer.kind == inner.kind && outer.width == inner.width &&
           outer.is_signed == inner.is_signed && outer.representation == inner.representation;
}

// Whether C++ performs arithmetic on this type only after promoting it to
// `int`. An update of such a local converts the promoted result back, which is
// a conversion C++L does not model.
bool promoted_before_arithmetic(CXType type) {
    const auto canonical = clang_getCanonicalType(type);
    if (canonical.kind == CXType_LValueReference || canonical.kind == CXType_RValueReference)
        type = reference_value_type(type);
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
            // A dereference is not merely an unmodeled operator. It awaits the
            // memory-validity obligations of RFC 0014: `p != nullptr` is
            // necessary and insufficient for a valid dereference, and the
            // pointer's state model may never supply the difference.
            if (clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_Deref)
                return "dereferencing a pointer requires the memory-validity obligations of RFC 0014, which are not "
                       "implemented; 'p != nullptr' alone does not establish that 'p' may be dereferenced";
            return "operator '" + take(clang_getUnaryOperatorKindSpelling(clang_getCursorUnaryOperatorKind(cursor))) +
                   "' is not modeled";
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
        case CXCursor_NullStmt:
            return "an empty statement";
        case CXCursor_GCCAsmStmt:
        case CXCursor_MSAsmStmt:
            return "inline assembly";
        default:
            return "'" + take(clang_getCursorKindSpelling(kind)) + "'";
    }
}

Expr build_expression(CXCursor cursor, const std::vector<CXCursor>& parameters, const Locals& locals, unsigned depth,
                      bool sequenced_call = false);

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

Expr build_expression(CXCursor cursor, const std::vector<CXCursor>& parameters, const Locals& locals, unsigned depth,
                      bool sequenced_call) {
    if (depth > kMaxExpressionDepth) {
        return unsupported_expression(cursor, "expression nests deeper than the bridge allows");
    }

    const CXCursorKind kind = clang_getCursorKind(cursor);

    const auto make_projection = [&](Expr subject, std::uint32_t index) -> Expr {
        if (index >= subject.type.projections.size())
            return unsupported_expression(cursor, "logical projection is outside the resolved signature");
        Expr result;
        result.type = subject.type.projections[index];
        result.location = presumed_location(clang_getCursorLocation(cursor));
        result.node = Projection{index, {std::move(subject)}};
        return result;
    };
    const auto strip = [](CXCursor value) {
        while (clang_getCursorKind(value) == CXCursor_UnexposedExpr ||
               clang_getCursorKind(value) == CXCursor_ParenExpr) {
            const auto nested = children_of(value);
            if (nested.size() != 1)
                break;
            value = nested[0];
        }
        return value;
    };
    if (kind == CXCursor_BinaryOperator && (clang_getCursorBinaryOperatorKind(cursor) == CXBinaryOperator_EQ ||
                                            clang_getCursorBinaryOperatorKind(cursor) == CXBinaryOperator_NE)) {
        const auto children = children_of(cursor);
        if (children.size() == 2) {
            for (unsigned side = 0; side != 2; ++side) {
                if (clang_getCursorKind(strip(children[side])) != CXCursor_CXXNullPtrLiteralExpr)
                    continue;
                Expr subject = build_expression(children[1 - side], parameters, locals, depth + 1);
                if (subject.type.representation.kind != source::RepresentationKind::Pointer)
                    continue;
                auto nullness = make_projection(std::move(subject), 0);
                if (clang_getCursorBinaryOperatorKind(cursor) == CXBinaryOperator_NE) {
                    Expr negated;
                    negated.type = nullness.type;
                    negated.location = nullness.location;
                    negated.node = Negation{{std::move(nullness)}};
                    return negated;
                }
                return nullness;
            }
        }
    }
    if (kind == CXCursor_MemberRefExpr) {
        const auto field = clang_getCursorReferenced(cursor);
        const auto children = children_of(cursor);
        if (clang_getCursorKind(field) == CXCursor_FieldDecl && children.size() == 1) {
            Expr subject = build_expression(children[0], parameters, locals, depth + 1);
            const auto& components = subject.type.representation.components;
            const auto name = take(clang_getCursorSpelling(field));
            for (std::size_t i = 0; i < components.size(); ++i)
                if (components[i].name == name && components[i].accessible)
                    return make_projection(std::move(subject), static_cast<std::uint32_t>(i));
        }
    }
    if (kind == CXCursor_ArraySubscriptExpr) {
        const auto children = children_of(cursor);
        if (children.size() == 2) {
            Expr subject = build_expression(strip(children[0]), parameters, locals, depth + 1);
            if (subject.type.representation.kind == source::RepresentationKind::Array) {
                if (CXEvalResult evaluated = clang_Cursor_Evaluate(children[1])) {
                    const bool integral = clang_EvalResult_getKind(evaluated) == CXEval_Int;
                    const auto index = integral ? clang_EvalResult_getAsLongLong(evaluated) : -1;
                    clang_EvalResult_dispose(evaluated);
                    if (index >= 0 && static_cast<std::size_t>(index) < subject.type.projections.size())
                        return make_projection(std::move(subject), static_cast<std::uint32_t>(index));
                }
                return unsupported_expression(cursor,
                                              "proof array index must be a constant within the resolved extent");
            }
        }
    }
    if (kind == CXCursor_CallExpr) {
        const auto called = clang_getCursorReferenced(cursor);
        const auto children = children_of(cursor);
        if (clang_getCursorKind(called) == CXCursor_CXXMethod && clang_Cursor_getNumArguments(cursor) == 0 &&
            !children.empty()) {
            const auto member = children_of(children[0]);
            if (member.size() == 1) {
                Expr subject = build_expression(member[0], parameters, locals, depth + 1);
                const auto family = subject.type.representation.kind;
                const auto name = take(clang_getCursorSpelling(called));
                if ((family == source::RepresentationKind::Optional ||
                     family == source::RepresentationKind::Expected) &&
                    name == "has_value")
                    return make_projection(std::move(subject), 0);
                if (family == source::RepresentationKind::Variant && name == "index")
                    return make_projection(std::move(subject), 0);
            }
        }
    }

    if (kind == CXCursor_ConditionalOperator) {
        const auto parts = children_of(cursor);
        if (parts.size() != 3)
            return unsupported_expression(cursor, "malformed conditional expression");
        Expr result;
        result.type = convert_type(clang_getCursorType(cursor));
        result.location = presumed_location(clang_getCursorLocation(cursor));
        Conditional choice;
        for (const auto& part : parts)
            choice.operands.push_back(build_expression(part, parameters, locals, depth + 1));
        result.node = std::move(choice);
        return result;
    }

    // Only the value-preserving scoped-enum -> exact underlying-type cast is
    // modeled. Clang resolves both types; all other casts still fail closed.
    if (kind == CXCursor_CXXStaticCastExpr) {
        const auto children = children_of(cursor);
        const auto operand = std::ranges::find_if(
            children, [](CXCursor child) { return clang_isExpression(clang_getCursorKind(child)) != 0; });
        if (operand != children.end()) {
            const Type destination = convert_type(clang_getCursorType(cursor));
            const Type source = convert_type(clang_getCursorType(*operand));
            if (!source.representation.identity.empty() && destination.representation.identity.empty() &&
                destination.kind == TypeKind::Int && destination.width == source.width &&
                destination.is_signed == source.is_signed) {
                Expr expression = build_expression(*operand, parameters, locals, depth + 1);
                expression.type = destination;
                return expression;
            }
        }
        return unsupported_expression(cursor, "only a scoped enum cast to its exact underlying type is modeled");
    }

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
        if (clang_getCursorKind(referenced) == CXCursor_EnumConstantDecl) {
            Expr expression;
            expression.type = convert_type(clang_getCursorType(cursor));
            expression.location = presumed_location(clang_getCursorLocation(cursor));
            expression.node = IntLiteral{static_cast<std::int64_t>(clang_getEnumConstantDeclValue(referenced))};
            return expression;
        }
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

    if (kind == CXCursor_IntegerLiteral || kind == CXCursor_CXXBoolLiteralExpr) {
        return build_integer_literal(cursor);
    }

    if (kind == CXCursor_CallExpr) {
        const CXCursor referenced = clang_getCursorReferenced(cursor);
        if (clang_Cursor_isNull(referenced) != 0 || clang_getCursorKind(referenced) != CXCursor_FunctionDecl) {
            return unsupported_expression(cursor, "call does not resolve to an ordinary function");
        }

        for (int index = 0; index < clang_Cursor_getNumArguments(referenced); ++index) {
            if (source::may_write(passing_of(
                    clang_getCursorType(clang_Cursor_getArgument(referenced, static_cast<unsigned>(index))))) &&
                !sequenced_call)
                return unsupported_expression(
                    cursor, "a mutating call requires a sequenced statement, initializer or assignment");
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
    if (const auto* unknown = std::get_if<UnknownVersion>(&expression.node); unknown && unknown->operands.size() == 1)
        return return_paths(unknown->operands.front());
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

std::optional<std::size_t> written_storage(CXCursor declaration, const Locals& locals, unsigned depth = 0) {
    if (const auto local = find_local(locals, declaration))
        return local;
    if (depth > kMaxExpressionDepth || clang_getCursorKind(declaration) != CXCursor_VarDecl)
        return std::nullopt;
    const auto type = clang_getCanonicalType(clang_getCursorType(declaration));
    if (type.kind != CXType_LValueReference && type.kind != CXType_RValueReference)
        return std::nullopt;
    auto initializer = clang_Cursor_getVarDeclInitializer(declaration);
    while (clang_getCursorKind(initializer) == CXCursor_UnexposedExpr ||
           clang_getCursorKind(initializer) == CXCursor_ParenExpr) {
        const auto inner = children_of(initializer);
        if (inner.size() != 1)
            return std::nullopt;
        initializer = inner[0];
    }
    return clang_getCursorKind(initializer) == CXCursor_DeclRefExpr
               ? written_storage(clang_getCursorReferenced(initializer), locals, depth + 1)
               : std::nullopt;
}

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
    if (kind == CXCursor_CallExpr) {
        const auto callee = clang_getCursorReferenced(cursor);
        const auto parameters = parameters_of(callee);
        if (std::ranges::any_of(parameters, [](CXCursor parameter) {
                return source::may_write(passing_of(clang_getCursorType(parameter)));
            })) {
            for (std::size_t index = 0; index < parameters.size(); ++index) {
                if (!source::aliases_storage(passing_of(clang_getCursorType(parameters[index]))))
                    continue;
                auto argument = clang_Cursor_getArgument(cursor, static_cast<unsigned>(index));
                while (clang_getCursorKind(argument) == CXCursor_UnexposedExpr ||
                       clang_getCursorKind(argument) == CXCursor_ParenExpr) {
                    const auto inner = children_of(argument);
                    if (inner.size() != 1)
                        break;
                    argument = inner.front();
                }
                if (auto storage = written_storage(clang_getCursorReferenced(argument), *scan.locals))
                    (*scan.written)[*storage] = true;
            }
        }
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
    if (const auto local = written_storage(clang_getCursorReferenced(target), *scan.locals)) {
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
    for (std::size_t target = 0; target < locals.size(); ++target) {
        if (!written[target] || !locals[target].external)
            continue;
        for (std::size_t other = 0; other < locals.size(); ++other)
            if (locals[other].external && !locals[other].referent &&
                same_modeled_value(locals[target].type, locals[other].type))
                written[other] = true;
    }
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
    bool executable_state = true;
    source::SourceLocation completion_location = {};

    bool has_post_state() const {
        return executable_state &&
               (result_type.kind == TypeKind::Void || std::ranges::any_of(parameters, [](CXCursor parameter) {
                    return source::aliases_storage(passing_of(clang_getCursorType(parameter)));
                }));
    }

    Expr completed(Expr value, const Locals& locals, CXCursor at) {
        if (!has_post_state())
            return value;
        ReturnState state;
        state.operands.push_back(std::move(value));
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const auto local = find_local(locals, parameters[index]);
            if (local && source::aliases_storage(passing_of(clang_getCursorType(parameters[index])))) {
                state.operands.push_back(local_read(locals[*local], at));
            } else {
                Expr input;
                input.type = convert_type(clang_getCursorType(parameters[index]), 0, ReferenceModel::Referent);
                input.location = presumed_location(clang_getCursorLocation(at));
                input.node =
                    ParameterRef{static_cast<std::uint32_t>(index), take(clang_getCursorSpelling(parameters[index]))};
                state.operands.push_back(std::move(input));
            }
        }
        Expr result;
        result.type = result_type;
        result.location =
            clang_Cursor_isNull(at) ? completion_location : presumed_location(clang_getCursorLocation(at));
        result.node = std::move(state);
        return result;
    }

    Expr void_value(CXCursor at) const {
        Expr value;
        value.type = result_type;
        value.location = clang_Cursor_isNull(at) ? completion_location : presumed_location(clang_getCursorLocation(at));
        value.node = IntLiteral{0};
        return value;
    }

    // Havoc uses the same version namespace as exact writes. No premise is
    // inherited for the new value; old facts still name only old versions.
    Expr unknown(const Local& storage, Expr body, CXCursor at) {
        Expr result;
        result.type = body.type;
        result.location = presumed_location(clang_getCursorLocation(at));
        result.node = UnknownVersion{storage.version, storage.type, {std::move(body)}};
        return result;
    }

    std::vector<std::size_t> invalidate_aliases(std::size_t storage, Locals& state) {
        std::vector<std::size_t> changed;
        if (!state[storage].external)
            return changed;
        for (std::size_t index = 0; index < state.size(); ++index) {
            if (index != storage && state[index].external && !state[index].referent &&
                same_modeled_value(state[index].type, state[storage].type)) {
                state[index].version = next_version++;
                changed.push_back(index);
            }
        }
        return changed;
    }

    // Evaluate a full expression once, then advance the storage touched by its
    // call. The continuation sees only these post-call versions.
    std::optional<Expr> evaluate(CXCursor cursor, Locals& state, std::vector<std::size_t>& invalidated) {
        while (clang_getCursorKind(cursor) == CXCursor_UnexposedExpr ||
               clang_getCursorKind(cursor) == CXCursor_ParenExpr) {
            const auto children = children_of(cursor);
            if (children.size() != 1 || !same_modeled_value(convert_type(clang_getCursorType(cursor)),
                                                            convert_type(clang_getCursorType(children.front()))))
                break;
            cursor = children.front();
        }
        Expr value = build_expression(cursor, parameters, state, 0, true);
        auto* call = std::get_if<Call>(&value.node);
        if (!call)
            return value;
        const auto callee = clang_getCursorReferenced(cursor);
        const auto params = parameters_of(callee);
        const bool writes = std::ranges::any_of(
            params, [](CXCursor parameter) { return source::may_write(passing_of(clang_getCursorType(parameter))); });
        if (!writes)
            return value;
        std::vector<std::size_t> targets;
        for (std::size_t index = 0; index < params.size(); ++index) {
            if (!source::aliases_storage(passing_of(clang_getCursorType(params[index]))))
                continue;
            const auto target = written_local(clang_Cursor_getArgument(cursor, static_cast<unsigned>(index)), state);
            if (!target)
                return std::nullopt;
            const auto storage = state[*target].referent.value_or(*target);
            // Shared actual arguments must share one post-state value.
            if (std::ranges::find(targets, storage) == targets.end()) {
                targets.push_back(storage);
                state[storage].version = next_version++;
            }
            call->effects.push_back(
                CallEffect{static_cast<std::uint32_t>(index), state[storage].version, state[storage].type});
        }
        for (std::size_t other = 0; other < state.size(); ++other) {
            if (!state[other].external || state[other].referent || std::ranges::find(targets, other) != targets.end())
                continue;
            if (std::ranges::any_of(targets, [&](std::size_t target) {
                    return state[target].external && same_modeled_value(state[target].type, state[other].type);
                })) {
                state[other].version = next_version++;
                invalidated.push_back(other);
            }
        }
        return value;
    }

    std::optional<Expr> lower_call(CXCursor statement, const Continuation& next, const Locals& locals, unsigned depth) {
        Locals state = locals;
        std::vector<std::size_t> invalidated;
        auto value = evaluate(statement, state, invalidated);
        if (!value)
            return std::nullopt;
        const auto version = next_version++;
        auto body = lower_statements(next, state, depth + 1);
        if (!body)
            return std::nullopt;
        for (auto index : invalidated)
            *body = unknown(state[index], std::move(*body), statement);
        return bind(version, "discarded call", std::move(*value), std::move(*body), statement);
    }

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
                if (result_type.kind == TypeKind::Void) {
                    const CXCursor at = clang_getNullCursor();
                    return completed(void_value(at), locals, at);
                }
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
        if (kind == CXCursor_CallExpr)
            return lower_call(statement, next, locals, depth);
        if (kind == CXCursor_NullStmt)
            return lower_statements(next, locals, depth + 1);
        if (kind == CXCursor_ReturnStmt) {
            const std::vector<CXCursor> returned = children_of(statement);
            if (returned.empty() && result_type.kind == TypeKind::Void)
                return completed(void_value(statement), locals, statement);
            if (returned.size() != 1)
                return reject("a return requires one value");
            Locals state = locals;
            std::vector<std::size_t> invalidated;
            auto value = evaluate(returned.front(), state, invalidated);
            if (!value)
                return std::nullopt;
            const auto* call = std::get_if<Call>(&value->node);
            if (call == nullptr || call->effects.empty())
                return completed(std::move(*value), state, statement);
            const auto version = next_version++;
            Expr read;
            read.type = value->type;
            read.location = value->location;
            read.node = LocalRef{version, "return value"};
            Expr body = completed(std::move(read), state, statement);
            for (auto changed : invalidated)
                body = unknown(state[changed], std::move(body), statement);
            return bind(version, "return value", std::move(*value), std::move(body), statement);
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
        const CXType written = clang_getCursorType(declaration);
        const auto canonical = clang_getCanonicalType(written);
        const bool reference = canonical.kind == CXType_LValueReference || canonical.kind == CXType_RValueReference;
        const CXType value_type = reference ? reference_value_type(written) : written;
        Type type = convert_type(value_type);
        // A verified body states a local as one modeled value under logical
        // versioning. A structural value has components rather than such a
        // value: it is a proof-side decomposition subject, not something this
        // body model can assign and re-read.
        if (type.kind == TypeKind::Unsupported || type.kind == TypeKind::Value) {
            return reject("local '" + name + "' has type '" + type.spelling + "', which is not modeled");
        }
        if (refinements != nullptr) {
            auto resolved = refinements_of(declaration, value_type, *refinements);
            if (!resolved)
                return reject(resolved.error());
            type.refinements = std::move(*resolved);
        }
        CXCursor initializer = clang_Cursor_getVarDeclInitializer(declaration);
        if (clang_Cursor_isNull(initializer) != 0) {
            return reject("local '" + name + "' is declared without an initializer, so it holds no modeled value");
        }
        std::optional<std::size_t> referent;
        if (reference) {
            CXCursor subject = initializer;
            while (clang_getCursorKind(subject) == CXCursor_UnexposedExpr ||
                   clang_getCursorKind(subject) == CXCursor_ParenExpr) {
                const auto inner = children_of(subject);
                if (inner.size() != 1)
                    break;
                subject = inner[0];
            }
            if (clang_getCursorKind(subject) == CXCursor_DeclRefExpr) {
                referent = find_local(locals, clang_getCursorReferenced(subject));
            }
            if (!referent) {
                return reject("reference '" + name +
                              "' must bind a tracked local object; this reference binding is not modeled");
            }
            if (!same_modeled_value(type, locals[*referent].type))
                return reject("reference binding changes the modeled value type");
        }
        if (clang_getCursorKind(initializer) == CXCursor_InitListExpr) {
            const std::vector<CXCursor> elements = children_of(initializer);
            if (elements.size() != 1) {
                return reject("the initializer of '" + name + "' is not a single modeled value");
            }
            initializer = elements[0];
        }
        Locals declaring = locals;
        std::vector<std::size_t> invalidated;
        auto evaluated = evaluate(initializer, declaring, invalidated);
        if (!evaluated)
            return std::nullopt;
        Expr value = std::move(*evaluated);
        if (!std::holds_alternative<Unsupported>(value.node) && !same_modeled_value(type, value.type)) {
            return reject("initializing '" + name + "' of type '" + type.spelling + "' from '" + value.type.spelling +
                          "' is a conversion that is not modeled");
        }
        const std::uint32_t version = next_version++;
        declaring.push_back(Local{declaration, version, type, referent});
        std::optional<Expr> body = lower_declaration(declared, index + 1, next, declaring, depth);
        if (!body) {
            return std::nullopt;
        }
        for (auto changed : invalidated)
            *body = unknown(declaring[changed], std::move(*body), declaration);
        return bind(version, name, std::move(value), std::move(*body), declaration, type);
    }

    // The local a write targets. Only a local of this body is ever written.
    std::optional<std::size_t> written_local(CXCursor target, const Locals& locals) {
        while (clang_getCursorKind(target) == CXCursor_ParenExpr ||
               clang_getCursorKind(target) == CXCursor_UnexposedExpr) {
            const std::vector<CXCursor> inner = children_of(target);
            if (inner.size() != 1) {
                return reject("this assignment target is not modeled");
            }
            target = inner[0];
        }
        if (clang_getCursorKind(target) == CXCursor_UnaryOperator &&
            clang_getCursorUnaryOperatorKind(target) == CXUnaryOperator_Deref) {
            return reject("writing through a pointer requires the memory-validity obligations of RFC 0014, which are "
                          "not implemented; 'p != nullptr' alone does not establish that 'p' may be written");
        }
        if (clang_getCursorKind(target) != CXCursor_DeclRefExpr) {
            return reject("only a local variable is assigned in a modeled body");
        }
        const CXCursor declaration = clang_getCursorReferenced(target);
        const std::string name = take(clang_getCursorSpelling(declaration));
        const std::optional<std::size_t> local = find_binding(locals, declaration);
        if (!local) {
            if (clang_getCursorKind(declaration) == CXCursor_ParmDecl) {
                return reject("parameter '" + name + "' has no modeled writable storage");
            }
            return reject("'" + name + "' is not a local of this body");
        }
        return local;
    }

    std::optional<Expr> write(std::size_t local, Expr value, CXCursor statement, const Continuation& next,
                              const Locals& locals, unsigned depth) {
        const std::uint32_t version = next_version++;
        Locals assigned = locals;
        const std::size_t storage = locals[local].referent.value_or(local);
        assigned[storage].version = version;
        const auto invalidated = invalidate_aliases(storage, assigned);
        std::optional<Expr> body = lower_statements(next, assigned, depth + 1);
        if (!body) {
            return std::nullopt;
        }
        // The version an assignment establishes is a value entering the local's
        // declared type exactly as the declaration's was, so it carries the same
        // type - refinement and all. Dropping it here would let a write into a
        // refined local escape the obligation its declaration owed (SPEC.md 17.2).
        Type required = locals[storage].type;
        auto require = [&](const Type& type) {
            for (const auto& refinement : type.refinements)
                if (std::ranges::find(required.refinements, refinement) == required.refinements.end())
                    required.refinements.push_back(refinement);
        };
        require(locals[local].type);
        for (const auto index : invalidated) {
            require(locals[index].type);
            *body = unknown(assigned[index], std::move(*body), statement);
        }
        return bind(version, take(clang_getCursorSpelling(locals[local].declaration)), std::move(value),
                    std::move(*body), statement, required);
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
        Locals state = locals;
        std::vector<std::size_t> invalidated;
        auto evaluated = evaluate(operands[1], state, invalidated);
        if (!evaluated)
            return std::nullopt;
        Expr value = std::move(*evaluated);
        if (!invalidated.empty())
            return reject("assignment call has uncertain aliases; use a separate call statement");
        if (!std::holds_alternative<Unsupported>(value.node) && !same_modeled_value(type, value.type)) {
            return reject("assigning '" + value.type.spelling + "' to '" +
                          take(clang_getCursorSpelling(locals[*local].declaration)) + "' of type '" + type.spelling +
                          "' is a conversion that is not modeled");
        }
        return write(*local, std::move(value), statement, next, state, depth);
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
        current.node = LocalRef{locals[target.referent.value_or(*local)].version, name};

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
                  const std::string& invariant_prefix, const std::vector<Selection::Refinement>& refinements,
                  bool executable_state) {
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
    BodyLowering lowering{parameters, function.result, invariant_prefix, &refinements, 0, 0, {}, {},
                          {},         executable_state};
    lowering.completion_location = presumed_location(clang_getRangeEnd(clang_getCursorExtent(members[body_index])));
    Locals candidates;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const auto& parameter = function.parameters[index];
        if (executable_state && (parameter.type.kind == TypeKind::Int || parameter.type.kind == TypeKind::Bool))
            candidates.push_back(
                Local{parameters[index], 0, parameter.type, std::nullopt, source::aliases_storage(parameter.passing)});
    }
    std::vector<bool> needed(candidates.size(), lowering.has_post_state());
    mark_writes(members[body_index], candidates, needed);
    WriteScan aliases{&candidates, &needed};
    clang_visitChildren(
        members[body_index],
        [](CXCursor child, CXCursor, CXClientData data) {
            auto& scan = *static_cast<WriteScan*>(data);
            if (clang_getCursorKind(child) == CXCursor_VarDecl &&
                source::aliases_storage(passing_of(clang_getCursorType(child)))) {
                if (auto target = written_storage(child, *scan.locals))
                    (*scan.written)[*target] = true;
            }
            return CXChildVisit_Recurse;
        },
        &aliases);
    Locals entry;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!needed[index])
            continue;
        candidates[index].version = lowering.next_version++;
        entry.push_back(candidates[index]);
    }
    function.returned_value = lowering.lower_statements(Continuation{nullptr, &statements, 0}, entry, 0);
    if (function.returned_value) {
        for (const auto& local : std::views::reverse(entry)) {
            const auto parameter = std::ranges::find_if(
                parameters, [&](CXCursor cursor) { return clang_equalCursors(cursor, local.declaration); });
            Expr value;
            value.type = local.type;
            value.location = function.location;
            value.node = ParameterRef{static_cast<std::uint32_t>(parameter - parameters.begin()),
                                      take(clang_getCursorSpelling(local.declaration))};
            *function.returned_value = lowering.bind(local.version, take(clang_getCursorSpelling(local.declaration)),
                                                     std::move(value), std::move(*function.returned_value), cursor);
        }
    }
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
    std::vector<CXCursor> functions;
    std::vector<CXCursor> unverified_storage;
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

    if (kind == CXCursor_Namespace || kind == CXCursor_UnexposedDecl || kind == CXCursor_LinkageSpec ||
        kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl || kind == CXCursor_ClassTemplate ||
        kind == CXCursor_UnionDecl) {
        return CXChildVisit_Recurse;
    }

    if (kind == CXCursor_FunctionDecl && is_selected(cursor, *collector.selection)) {
        collector.selected.push_back(cursor);
    }
    if (kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod || kind == CXCursor_FunctionTemplate ||
        kind == CXCursor_Constructor || kind == CXCursor_Destructor) {
        collector.functions.push_back(cursor);
        const auto name = take(clang_getCursorSpelling(cursor));
        const bool generated = !collector.selection->specification_prefix.empty() &&
                               name.starts_with(collector.selection->specification_prefix);
        const bool verified = std::ranges::find(collector.selection->verified_offsets, physical_offset(cursor)) !=
                              collector.selection->verified_offsets.end();
        return generated || verified ? CXChildVisit_Continue : CXChildVisit_Recurse;
    }
    if (kind == CXCursor_VarDecl || kind == CXCursor_FieldDecl)
        collector.unverified_storage.push_back(cursor);

    return collector.selection->refinements.empty() ? CXChildVisit_Continue : CXChildVisit_Recurse;
}

// Detect an explicit refinement use at an unverified storage/callable boundary.
// These are Clang declaration-reference edges, including ordinary aliases and
// type constructors; no pointer/pointee or container-wide fact is inferred.
std::optional<std::string> refinement_use(CXCursor declaration, const Selection& selection, unsigned depth = 0) {
    if (depth > kMaxExpressionDepth)
        return "unresolved alias chain";
    const auto entry =
        std::ranges::find(selection.refinements, physical_offset(declaration), &Selection::Refinement::alias_offset);
    if (entry != selection.refinements.end())
        return entry->name;
    const auto initializer = clang_Cursor_getVarDeclInitializer(declaration);
    for (const auto child : children_of(declaration)) {
        const auto kind = clang_getCursorKind(child);
        if ((!clang_Cursor_isNull(initializer) && clang_equalCursors(initializer, child)) ||
            kind == CXCursor_ParmDecl || clang_isStatement(kind))
            break;
        if (kind == CXCursor_TypeRef || kind == CXCursor_TemplateRef) {
            const auto referenced = clang_getCursorReferenced(child);
            const auto referenced_kind = clang_getCursorKind(referenced);
            if (referenced_kind == CXCursor_TypeAliasDecl || referenced_kind == CXCursor_TypedefDecl ||
                referenced_kind == CXCursor_TypeAliasTemplateDecl) {
                if (auto use = refinement_use(referenced, selection, depth + 1))
                    return use;
            }
        }
        if (kind == CXCursor_TypeAliasDecl) {
            if (auto use = refinement_use(child, selection, depth + 1))
                return use;
        }
    }
    return std::nullopt;
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
        // The equality helper takes its operands by reference so it imposes no
        // copy on the values compared. The operand type is the referent's.
        FormalEquality equality{convert_type(first, 0, ReferenceModel::Referent), {}};
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
    CXUnsavedFile unsaved{};
    if (request.content) {
        unsaved.Filename = request.path.c_str();
        unsaved.Contents = request.content->data();
        unsaved.Length = static_cast<unsigned long>(request.content->size());
    }
    const CXErrorCode error = clang_parseTranslationUnit2(
        index, request.path.c_str(), argv.data(), static_cast<int>(argv.size()), request.content ? &unsaved : nullptr,
        request.content ? 1u : 0u, CXTranslationUnit_None, &unit);
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
    if (result.has_errors && !request.recover_bindings && !request.recover_contract_types)
        return result;

    Collector collector;
    collector.selection = &request.selection;
    clang_visitChildren(clang_getTranslationUnitCursor(unit), collect, &collector);
    if (result.has_errors && request.recover_contract_types && !request.recover_bindings) {
        // Recover only canonical void return identities, never bodies, layout,
        // obligations or facts from an erroneous AST. The corrected projection
        // must pass a fresh Clang analysis before verification can proceed.
        for (const auto cursor : collector.selected) {
            const auto offset = physical_offset(cursor);
            if (std::ranges::find(request.selection.verified_offsets, offset) ==
                    request.selection.verified_offsets.end() ||
                clang_getCanonicalType(clang_getCursorResultType(cursor)).kind != CXType_Void)
                continue;
            Function function;
            function.analysis_offset = offset;
            function.result.kind = TypeKind::Void;
            result.functions.push_back(std::move(function));
        }
        return result;
    }

    // An erased return alias is not evidence. Check even ordinary declarations
    // that were not selected for body elaboration. A verified redeclaration may
    // establish the same callable only through Clang's declaration identity.
    for (const auto cursor : collector.functions) {
        if (request.selection.refinements.empty())
            break;
        const auto name = take(clang_getCursorSpelling(cursor));
        if (!request.selection.specification_prefix.empty() && name.starts_with(request.selection.specification_prefix))
            continue;
        const auto refined = refinement_use(cursor, request.selection);
        if (!refined)
            continue;
        const auto canonical = clang_getCanonicalCursor(cursor);
        const bool verified = std::ranges::any_of(collector.selected, [&](CXCursor candidate) {
            return clang_equalCursors(canonical, clang_getCanonicalCursor(candidate)) &&
                   std::ranges::find(request.selection.verified_offsets, physical_offset(candidate)) !=
                       request.selection.verified_offsets.end();
        });
        if (!verified) {
            result.has_errors = true;
            result.diagnostics.push_back(
                {Severity::Error,
                 "ordinary function '" + qualified_name_of(cursor) + "' return cannot establish refinement '" +
                     *refined + "'; verify its definition (explicit trusted refinement boundaries are not implemented)",
                 presumed_location(clang_getCursorLocation(cursor))});
        }
    }
    for (const auto declaration : collector.unverified_storage) {
        if (request.selection.refinements.empty())
            break;
        if (const auto refined = refinement_use(declaration, request.selection)) {
            result.has_errors = true;
            result.diagnostics.push_back(
                {Severity::Error,
                 "storage '" + take(clang_getCursorSpelling(declaration)) + "' uses refinement '" + *refined +
                     "' outside a modeled verified body; its construction and mutations require proof",
                 presumed_location(clang_getCursorLocation(declaration))});
        }
    }

    for (const CXCursor& cursor : collector.selected) {
        Function function;
        function.usr = take(clang_getCursorUSR(cursor));
        function.name = take(clang_getCursorSpelling(cursor));
        function.qualified_name = qualified_name_of(cursor);
        // A projected proof expression returns `decltype(auto)` over a
        // parenthesized expression, so Clang gives it a reference type whenever
        // the expression is a glvalue. That reference is an artifact of how the
        // expression is handed to Clang, not something the author wrote, and the
        // value denoted is the subject's own. An ordinary declaration's result
        // and parameters keep reference types opaque, so a contract is never
        // proven about a value another object can change (AGENTS.md 11).
        const bool projected_expression = !request.selection.specification_prefix.empty() &&
                                          function.name.starts_with(request.selection.specification_prefix);
        function.result = convert_type(clang_getCursorResultType(cursor), 0,
                                       projected_expression ? ReferenceModel::Referent : ReferenceModel::Opaque);
        function.location = presumed_location(clang_getCursorLocation(cursor));
        function.analysis_offset = physical_offset(cursor);

        // A refinement on a parameter or a result is verification-level identity
        // Clang canonicalizes away, so it is recovered from the written type here
        // (SPEC.md 17.3): a refined parameter carries its predicate into the body,
        // and a refined result states one at every return.
        const auto attach_refinements = [&](Type& type, CXCursor declaration, CXType written) {
            auto resolved = refinements_of(declaration, written, request.selection.refinements);
            if (resolved) {
                type.refinements = std::move(*resolved);
            } else {
                result.has_errors = true;
                result.diagnostics.push_back(
                    {Severity::Error, resolved.error(), presumed_location(clang_getCursorLocation(declaration))});
            }
        };
        attach_refinements(function.result, cursor, clang_getCursorResultType(cursor));

        const std::vector<CXCursor> parameter_cursors = parameters_of(cursor);
        for (const CXCursor& parameter : parameter_cursors) {
            // A proof binder is projected as a reference parameter so Clang
            // resolves it without requiring a copy, a move, a default
            // constructor or any runtime object. It denotes the subject's own
            // value, so the referent is what it means.
            const auto written = clang_getCursorType(parameter);
            const auto passing = passing_of(written);
            Type parameter_type = convert_type(written, 0, ReferenceModel::Referent);
            attach_refinements(parameter_type, parameter,
                               source::aliases_storage(passing) ? reference_value_type(written) : written);
            function.parameters.push_back(
                Parameter{take(clang_getCursorSpelling(parameter)), std::move(parameter_type), passing});
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
                         request.selection.refinements,
                         std::ranges::find(request.selection.verified_offsets, function.analysis_offset) !=
                             request.selection.verified_offsets.end());
        }
        result.functions.push_back(std::move(function));
    }

    return result;
}

std::string clang_version() {
    return take(clang_getClangVersion());
}

} // namespace cppl::clangbridge
