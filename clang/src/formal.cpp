#include "formal.hpp"

#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/projection.hpp"
#include "cppl/source/representation.hpp"
#include "expressions.hpp"
#include "places.hpp"
#include "refinements.hpp"
#include "signature.hpp"
#include "types.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <expected>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// A contract clause as the projector emitted it (SPEC.md 12.10, ARCHITECTURE.md
// 25): its formal proposition, built from the shape the projector recorded with
// every C++ leaf resolved by Clang, and its memory capabilities, read apart
// from the predicates they are conjoined with.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::strip_parens;
using bridge::take;

namespace {

// The schema describes only syntax emitted by the projector. Every C++ leaf,
// parameter type and declaration reference is resolved independently by Clang.
std::expected<Expr, std::string> build_formal(CXCursor cursor, const source::ProjectionShape& shape,
                                              const Signature& signature, unsigned depth) {
    using Kind = source::ProjectionKind;
    if (depth > kMaxExpressionDepth)
        return std::unexpected("formal proposition nests too deeply");
    // A capability is a statement about storage, not a value, so it cannot be an
    // operand of a logical connective that the kernel would then have to check.
    // Combining capabilities is a contract-level matter: state them as separate
    // clauses (SPEC.md 12.10).
    if (shape.kind == Kind::Readable || shape.kind == Kind::Writable || shape.kind == Kind::Capabilities)
        return std::unexpected("a memory capability states storage permission, not a value, so it cannot be an "
                               "operand of a proposition");
    if (shape.kind == Kind::Expression) {
        if (!shape.children.empty())
            return std::unexpected("malformed expression projection");
        return build_expression(cursor, signature, {}, 0);
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
        // copy on the values compared. The operand type is the referent's, with
        // the refinement it is written as, which obligation construction must
        // see to refuse an equality between values of a refinement type.
        FormalEquality equality{convert_type(first, 0, ReferenceModel::Referent, &signature.refinements), {}};
        auto refined =
            refinements_of(formals[0], clang_getPointeeType(clang_getCursorType(formals[0])), signature.refinements);
        if (!refined)
            return std::unexpected("formal equality operand type has " + refined.error().message);
        equality.operand_type.refinements = std::move(*refined);
        // The first operator() argument is the closure object.
        for (unsigned index = 1; index < 3; ++index)
            equality.operands.push_back(build_expression(clang_Cursor_getArgument(cursor, index), signature, {}, 0));
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
        Signature scope = signature;
        scope.parameters.insert(scope.parameters.end(), binders.begin(), binders.end());
        auto body = build_formal(values[0], shape.children[0], scope, depth + 1);
        if (!body)
            return body;
        // A binder ranges over the values of the type it is written with, so a
        // refinement is recovered from the written type exactly as a
        // parameter's is (SPEC.md FORALL-001).
        Universal quantified;
        for (const auto binder : binders) {
            Type binder_type = convert_type(clang_getCursorType(binder));
            auto refined = refinements_of(binder, clang_getCursorType(binder), signature.refinements);
            if (!refined)
                return std::unexpected("forall binder '" + take(clang_getCursorSpelling(binder)) + "' has " +
                                       refined.error().message);
            binder_type.refinements = std::move(*refined);
            quantified.binders.push_back(std::move(binder_type));
        }
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
            auto operand = build_formal(statements[index], shape.children[index], signature, depth + 1);
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

// A capability probe's body is the projected `([](auto&&...) {})(operands)`:
// a lambda that is declared, called for its operand types and does nothing.
// Decoding it yields the capability's operands, resolved by Clang, and never an
// `Expr` that could reach the kernel.
std::expected<Capability, std::string> build_capability(CXCursor cursor, source::ProjectionKind kind,
                                                        const Signature& signature) {
    const std::vector<CXCursor>& parameters = signature.parameters;
    while (clang_getCursorKind(cursor) == CXCursor_UnexposedExpr || clang_getCursorKind(cursor) == CXCursor_ParenExpr) {
        const auto children = children_of(cursor);
        if (children.size() != 1)
            return std::unexpected("malformed memory capability wrapper");
        cursor = children[0];
    }
    if (clang_getCursorKind(cursor) != CXCursor_CallExpr)
        return std::unexpected("malformed memory capability probe");
    // The first argument of the projected call is the closure object; the
    // capability's own operands follow it.
    const int arguments = clang_Cursor_getNumArguments(cursor);
    if (arguments != 2 && arguments != 3)
        return std::unexpected("a memory capability states a pointer and an optional element count");
    Capability capability;
    capability.kind =
        kind == source::ProjectionKind::Readable ? Capability::Kind::Readable : Capability::Kind::Writable;
    capability.location = presumed_location(clang_getCursorLocation(cursor));

    // In a contract the capability's pointer is one of the function's
    // parameters, so the place it names is that parameter's storage. Resolving
    // it here keeps Clang the authority on which declaration the spelling
    // refers to.
    CXCursor pointer = clang_Cursor_getArgument(cursor, 1);
    while (clang_getCursorKind(pointer) == CXCursor_UnexposedExpr ||
           clang_getCursorKind(pointer) == CXCursor_ParenExpr) {
        const auto nested = children_of(pointer);
        if (nested.size() != 1)
            break;
        pointer = nested[0];
    }
    if (clang_getCursorKind(pointer) != CXCursor_DeclRefExpr)
        return std::unexpected("a memory capability names a pointer parameter");
    const CXCursor declaration = clang_getCursorReferenced(pointer);
    const auto at = std::ranges::find_if(
        parameters, [&](CXCursor candidate) { return clang_equalCursors(candidate, declaration) != 0; });
    if (at == parameters.end())
        return std::unexpected("a memory capability names a pointer parameter of this function");
    // A span parameter states its own extent, so its capability covers every
    // element it views and takes no count (SPEC.md STDMODEL-016).
    const Type named = convert_type(clang_getCursorType(declaration));
    const bool span = named.representation.kind == source::RepresentationKind::Span;
    if (span && !named.representation.rejection.empty())
        return std::unexpected("'" + named.spelling + "' is not modeled: " + named.representation.rejection);
    if (span && arguments != 2)
        return std::unexpected("a span states its own extent, so a capability of a span takes no element count");
    const CXType canonical = clang_getCanonicalType(clang_getCursorType(declaration));
    if (!span && canonical.kind != CXType_Pointer)
        return std::unexpected("a memory capability names a pointer or a span");
    // Write access is a property of the access path, never of the storage it
    // reaches: a span or pointer of const elements grants none, whatever the
    // storage behind it permits (SPEC.md VERIFIED-036, STDMODEL-016).
    const CXType element = span ? clang_Type_getTemplateArgumentAsType(canonical, 0) : clang_getPointeeType(canonical);
    if (capability.kind == Capability::Kind::Writable && clang_isConstQualifiedType(element) != 0)
        return std::unexpected("'writable(" + take(clang_getCursorSpelling(declaration)) +
                               ")' names elements declared const; no write is permitted through '" + named.spelling +
                               "', so it can only be 'readable'");
    capability.pointer.root.kind = PlaceRoot::Kind::Parameter;
    // The callable position, past a member function's implicit object.
    capability.pointer.root.id = signature.position(static_cast<std::size_t>(at - parameters.begin()));
    capability.pointer.spelling = take(clang_getCursorSpelling(declaration));

    if (arguments == 3)
        capability.extent.push_back(build_expression(clang_Cursor_getArgument(cursor, 2), signature, {}, 0));
    return capability;
}

// A capability clause is either one capability or a conjunction of them, which
// the projector emitted as a lambda holding one statement per operand.
std::expected<std::vector<Capability>, std::string> build_capabilities(CXCursor cursor,
                                                                       const source::ProjectionShape& shape,
                                                                       const Signature& signature, unsigned depth) {
    if (depth > kMaxExpressionDepth)
        return std::unexpected("memory capabilities nest too deeply");
    if (shape.kind != source::ProjectionKind::Capabilities) {
        auto one = build_capability(cursor, shape.kind, signature);
        if (!one)
            return std::unexpected(one.error());
        return std::vector<Capability>{std::move(*one)};
    }
    while (clang_getCursorKind(cursor) == CXCursor_UnexposedExpr || clang_getCursorKind(cursor) == CXCursor_ParenExpr) {
        const auto nested = children_of(cursor);
        if (nested.size() != 1)
            return std::unexpected("malformed memory capability wrapper");
        cursor = nested[0];
    }
    if (clang_getCursorKind(cursor) != CXCursor_LambdaExpr || shape.children.size() != 2)
        return std::unexpected("malformed conjunction of memory capabilities");
    std::vector<CXCursor> bodies;
    for (const auto child : children_of(cursor)) {
        if (clang_getCursorKind(child) == CXCursor_CompoundStmt)
            bodies.push_back(child);
    }
    if (bodies.size() != 1)
        return std::unexpected("a conjunction of memory capabilities requires one body");
    const auto statements = children_of(bodies[0]);
    if (statements.size() != 2)
        return std::unexpected("a conjunction of memory capabilities requires two operands");
    std::vector<Capability> capabilities;
    for (std::size_t index = 0; index < 2; ++index) {
        auto operand = build_capabilities(statements[index], shape.children[index], signature, depth + 1);
        if (!operand)
            return operand;
        capabilities.insert(capabilities.end(), std::make_move_iterator(operand->begin()),
                            std::make_move_iterator(operand->end()));
    }
    return capabilities;
}

bool is_capability_shape(const source::ProjectionShape& shape) {
    return shape.kind == source::ProjectionKind::Readable || shape.kind == source::ProjectionKind::Writable ||
           shape.kind == source::ProjectionKind::Capabilities;
}

// A conjunction of memory capabilities and ordinary predicates, read apart
// (SPEC.md STDMODEL-016, ARCHITECTURE.md 25): each capability leaves on the
// capability channel, and the predicates, conjoined in the order written, are
// the proposition the clause states to the kernel. The two channels together
// mean exactly the conjunction: a caller owes every part, and the body supposes
// every part.
std::expected<void, std::string> split_conjunction(CXCursor cursor, const source::ProjectionShape& shape,
                                                   const Signature& signature, std::vector<Capability>& capabilities,
                                                   std::vector<Expr>& propositions, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return std::unexpected("formal proposition nests too deeply");
    }
    if (is_capability_shape(shape)) {
        auto found = build_capabilities(cursor, shape, signature, depth + 1);
        if (!found) {
            return std::unexpected(found.error());
        }
        capabilities.insert(capabilities.end(), std::make_move_iterator(found->begin()),
                            std::make_move_iterator(found->end()));
        return {};
    }
    if (!mixes_capabilities(shape)) {
        auto proposition = build_formal(cursor, shape, signature, depth + 1);
        if (!proposition) {
            return std::unexpected(proposition.error());
        }
        propositions.push_back(std::move(*proposition));
        return {};
    }
    cursor = strip_parens(cursor);
    if (clang_getCursorKind(cursor) != CXCursor_LambdaExpr || shape.children.size() != 2) {
        return std::unexpected("malformed conjunction of a memory capability and a predicate");
    }
    std::vector<CXCursor> bodies;
    for (const auto child : children_of(cursor)) {
        if (clang_getCursorKind(child) == CXCursor_CompoundStmt) {
            bodies.push_back(child);
        }
    }
    if (bodies.size() != 1 || children_of(bodies.front()).size() != 2) {
        return std::unexpected("malformed conjunction of a memory capability and a predicate");
    }
    const std::vector<CXCursor> operands = children_of(bodies.front());
    for (std::size_t index = 0; index < 2; ++index) {
        if (auto split = split_conjunction(operands[index], shape.children[index], signature, capabilities,
                                           propositions, depth + 1);
            !split) {
            return split;
        }
    }
    return {};
}

} // namespace

// Whether a conjunction joins a memory capability with an ordinary predicate
// somewhere among its conjuncts.
bool mixes_capabilities(const source::ProjectionShape& shape, unsigned depth) {
    if (depth > kMaxExpressionDepth || shape.kind != source::ProjectionKind::Conjunction) {
        return false;
    }
    return std::ranges::any_of(shape.children, [&](const source::ProjectionShape& child) {
        return is_capability_shape(child) || mixes_capabilities(child, depth + 1);
    });
}

void extract_formal(Function& function, CXCursor cursor, const Signature& signature,
                    const source::ProjectionShape& shape) {
    function.has_body = true;
    function.body_rejection = "malformed formal proposition probe";
    const bool is_capability = shape.kind == source::ProjectionKind::Readable ||
                               shape.kind == source::ProjectionKind::Writable ||
                               shape.kind == source::ProjectionKind::Capabilities;
    const bool mixed = mixes_capabilities(shape);
    for (const auto child : children_of(cursor)) {
        if (clang_getCursorKind(child) != CXCursor_CompoundStmt)
            continue;
        const auto statements = children_of(child);
        if (statements.size() != 1)
            return;
        // A capability probe states no value, so its body is the projected call
        // as a statement rather than a return.
        if (is_capability) {
            auto capabilities = build_capabilities(statements[0], shape, signature, 0);
            if (!capabilities) {
                function.body_rejection = capabilities.error();
                return;
            }
            function.capabilities = std::move(*capabilities);
            function.body_rejection.reset();
            return;
        }
        if (clang_getCursorKind(statements[0]) != CXCursor_ReturnStmt)
            return;
        const auto values = children_of(statements[0]);
        if (values.size() != 1)
            return;
        if (mixed) {
            std::vector<Capability> capabilities;
            std::vector<Expr> propositions;
            if (auto split = split_conjunction(values[0], shape, signature, capabilities, propositions, 0); !split) {
                function.body_rejection = split.error();
                return;
            }
            if (propositions.empty() || capabilities.empty()) {
                return;
            }
            Expr conjoined = std::move(propositions.front());
            for (std::size_t index = 1; index < propositions.size(); ++index) {
                Expr next;
                next.type.kind = TypeKind::Proposition;
                next.type.spelling = "Prop";
                next.location = conjoined.location;
                next.node =
                    Connective{Connective::Kind::Conjunction, {std::move(conjoined), std::move(propositions[index])}};
                conjoined = std::move(next);
            }
            function.capabilities = std::move(capabilities);
            function.returned_value = std::move(conjoined);
            function.body_rejection.reset();
            return;
        }
        auto expression = build_formal(values[0], shape, signature, 0);
        if (!expression) {
            function.body_rejection = expression.error();
            return;
        }
        function.returned_value = std::move(*expression);
        function.body_rejection.reset();
        return;
    }
}

} // namespace cppl::clangbridge::detail
