#include "sequences.hpp"

#include "access.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
#include "expressions.hpp"
#include "places.hpp"
#include "refinements.hpp"
#include "signature.hpp"
#include "types.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// The standard-sequence model's reading of a call (RFC 0020): which operation
// of a vector, a string, a span or a `std::array` a call is, where the elements
// a subscript selects live and what bounds its index, the library summary a
// call is lowered as, and an operation read as a value.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::strip_parens;
using bridge::take;

namespace {

// Whether an allocator argument can have no effect: a default argument, or a
// `std::allocator` value-initialized in place, possibly behind the implicit
// nodes that bind it to a reference.
bool inert_allocator(CXCursor argument, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return false;
    }
    const CXCursorKind kind = clang_getCursorKind(argument);
    if (kind == CXCursor_CallExpr) {
        return clang_getCursorKind(clang_getCursorReferenced(argument)) == CXCursor_Constructor &&
               clang_Cursor_getNumArguments(argument) == 0 &&
               is_standard_template(clang_getCursorType(argument), "allocator");
    }
    if (kind == CXCursor_UnexposedExpr || kind == CXCursor_ParenExpr || kind == CXCursor_InitListExpr) {
        return std::ranges::all_of(children_of(argument),
                                   [depth](CXCursor child) { return inert_allocator(child, depth + 1); });
    }
    return false;
}

// The identity of a library summary: its operation and the resolved type it is
// stated at (RFC 0020 §6). It names the summary, never a declaration, and is
// derived from Clang's USR of the specialization so two element types never
// share one. The mark is analysis only: the program keeps its own call, and
// nothing about it reaches the runtime translation unit (SPEC.md STDMODEL-022).
std::string library_symbol(source::LibraryCall library, const Type& container) {
    return "cppl-library:" + std::string(source::describe_model(library.container)) +
           "::" + std::string(source::describe(library.operation)) + "#" + container.representation.identity;
}

// A span of a whole tracked container, formed where a call converts the
// container to the span a parameter takes (RFC 0020 §6, §7). Its length is the
// container's; its elements are the container's, which the caller owns or
// borrows by reference, so it is live for the call.
Expr view_argument(CXCursor source, const Type& span, const Locals& locals, CXCursor at) {
    if (!span.representation.rejection.empty()) {
        return unsupported_expression(at, "'" + span.spelling + "' is not modeled: " + span.representation.rejection);
    }
    const std::optional<std::size_t> root = owning_root(source, locals);
    if (!root) {
        return unsupported_expression(at, "a span is modeled only over a whole vector or string this body tracks");
    }
    Expr container;
    container.type = locals[*root].type;
    container.location = presumed_location(clang_getCursorLocation(source));
    container.node = PlaceRef{locals[*root].version, place_of(locals, *root)};
    return library_call({source::RepresentationKind::Span, source::LibraryOperation::ViewOf}, span,
                        "std::span(" + locals[*root].spelling + ")", {std::move(container)}, span, at);
}

} // namespace

// A member of a modeled sequence as a diagnostic and the trust report name it:
// the standard name, without the inline namespace a library puts it in, so the
// same program is described alike whichever library it is compiled against.
std::string library_name(source::RepresentationKind family, const std::string& member) {
    return std::string(source::describe_model(family)) + "::" + member;
}

std::optional<SequenceCall> sequence_call(CXCursor cursor) {
    if (clang_getCursorKind(cursor) != CXCursor_CallExpr) {
        return std::nullopt;
    }
    const CXCursor method = clang_getCursorReferenced(cursor);
    const CXCursorKind kind = clang_getCursorKind(method);
    if (kind != CXCursor_CXXMethod && kind != CXCursor_Constructor) {
        return std::nullopt;
    }
    const source::RepresentationKind family = library_kind(clang_getCursorSemanticParent(method));
    if (!source::is_sequence(family) && family != source::RepresentationKind::StdArray) {
        return std::nullopt;
    }
    SequenceCall call;
    call.method = method;
    call.family = family;
    call.name = take(clang_getCursorSpelling(method));
    call.constructor = kind == CXCursor_Constructor;
    const int count = clang_Cursor_getNumArguments(cursor);
    if (count < 0) {
        return std::nullopt;
    }
    std::size_t first = 0;
    if (!call.constructor) {
        const std::vector<CXCursor> children = children_of(cursor);
        if (children.empty()) {
            return std::nullopt;
        }
        if (clang_getCursorKind(children.front()) == CXCursor_MemberRefExpr &&
            clang_equalCursors(clang_getCursorReferenced(children.front()), method) != 0) {
            const std::vector<CXCursor> member = children_of(children.front());
            if (member.size() != 1) {
                return std::nullopt;
            }
            call.object = member.front();
        } else {
            // An operator's object is its first argument.
            if (count < 1) {
                return std::nullopt;
            }
            call.object = clang_Cursor_getArgument(cursor, 0);
            first = 1;
        }
    }
    for (auto index = static_cast<unsigned>(first); index < static_cast<unsigned>(count); ++index) {
        call.arguments.push_back(clang_Cursor_getArgument(cursor, index));
    }
    // A library may give a constructor a trailing allocator parameter with a
    // default argument, as libstdc++ does where libc++ declares a separate
    // overload. That allocator is `std::allocator`, the only one a modeled
    // sequence has (STDMODEL-010), whose instances are interchangeable: it
    // states nothing the model speaks of, so it is not an operand of the
    // operation. Only an argument that cannot have an effect is set aside --
    // the default, or `std::allocator<T>{}` written in place; any other keeps
    // its place and the constructor is refused as unmodeled.
    if (call.constructor) {
        while (!call.arguments.empty()) {
            const int position = static_cast<int>(first + call.arguments.size()) - 1;
            if (position >= clang_Cursor_getNumArguments(method)) {
                break;
            }
            const CXType formal = clang_getCanonicalType(
                clang_getCursorType(clang_Cursor_getArgument(method, static_cast<unsigned>(position))));
            const CXType held = formal.kind == CXType_LValueReference ? clang_getPointeeType(formal) : formal;
            if (!is_standard_template(held, "allocator") || !inert_allocator(call.arguments.back(), 0)) {
                break;
            }
            call.arguments.pop_back();
        }
    }
    return call;
}

// Observe one element of an array value at a symbolic index, with the bounds
// obligation the subscript owes (FOUNDATIONS.md 45, SPEC.md STORAGE-005).
//
// The extent is the resolved type's own component count, so it is available
// before any element has been observed. The obligation is the same
// `ElementBound` a tracked subscript owes, stated at the same index term this
// observation reads (ARCHITECTURE.md ARCH-ELEM-003, ARCH-ELEM-004).
Expr element_observation(Expr subject, CXCursor index_cursor, const Signature& signature, const Locals& locals,
                         unsigned depth, CXCursor cursor) {
    const std::size_t extent = subject.type.projections.size();
    if (extent == 0) {
        return unsupported_expression(cursor, "this subscript's array has no modeled extent");
    }
    Expr index = build_expression(index_cursor, signature, locals, depth + 1);
    if (index.type.kind != TypeKind::Int) {
        return unsupported_expression(cursor, "a symbolic array index must be an integer this implementation models");
    }
    // The extent enters the comparison at the index's own type, as it does for
    // a tracked subscript: a differing type is a conversion this implementation
    // does not model, and the obligation refuses it rather than inventing one.
    Expr count;
    count.type = index.type;
    count.location = index.location;
    count.node = IntLiteral{static_cast<std::int64_t>(extent)};

    Expr observed;
    observed.type = subject.type.projections.front();
    observed.location = presumed_location(clang_getCursorLocation(cursor));
    observed.node = Element{{std::move(subject), index}};

    Expr bound;
    bound.type = observed.type;
    bound.location = index.location;
    bound.node = ElementBound{{std::move(count)}, {std::move(index), std::move(observed)}};
    return bound;
}

std::expected<ElementRegion, std::string> element_region(CXCursor object, const Locals& locals,
                                                         const Signature& signature) {
    const std::vector<CXCursor>& parameters = signature.parameters;
    object = strip_parens(object);
    if (clang_getCursorKind(object) != CXCursor_DeclRefExpr) {
        return std::unexpected(
            std::string("a subscript of a container is modeled only on a local or parameter this body names directly"));
    }
    const CXCursor declaration = clang_getCursorReferenced(object);
    const std::string spelled = take(clang_getCursorSpelling(declaration));
    if (const auto binding = find_binding(locals, declaration)) {
        if (auto stale = stale_borrow(locals, *binding)) {
            return std::unexpected(std::move(*stale));
        }
        const std::size_t storage = locals[*binding].referent.value_or(*binding);
        const Local& accessed = locals[storage];
        if (!accessed.sequence.has_value()) {
            return std::unexpected("'" + spelled + "' is not a container this body tracks");
        }
        ElementRegion region;
        region.accessed = storage;
        const std::size_t root = accessed.sequence->views.value_or(storage);
        region.root = root;
        const Local* owner_entry = root < locals.size() ? &locals[root] : nullptr;
        if (owner_entry == nullptr || !owner_entry->sequence.has_value()) {
            return std::unexpected("the storage '" + spelled + "' views is not tracked by this body");
        }
        region.declaration = owner_entry->declaration;
        region.element = owner_entry->sequence->element;
        region.external = owner_entry->sequence->external_elements;
        region.family = accessed.sequence->kind;
        return region;
    }
    // A span parameter is not tracked: it is a value the call fixed, and its
    // elements are storage of the caller's (RFC 0020 §7).
    const auto at = std::ranges::find_if(
        parameters, [&](CXCursor candidate) { return clang_equalCursors(candidate, declaration); });
    if (at != parameters.end()) {
        const Type type = convert_type(clang_getCursorType(declaration));
        if (type.representation.kind == source::RepresentationKind::Span && type.representation.rejection.empty()) {
            auto element = sequence_element(declaration, clang_getCursorType(declaration), nullptr);
            if (!element) {
                return std::unexpected("span parameter '" + spelled + "': " + element.error());
            }
            ElementRegion region;
            region.declaration = declaration;
            // By its callable position, past a member function's implicit
            // object, as a capability names it (SPEC.md CLASS-008).
            region.parameter = signature.position(static_cast<std::size_t>(at - parameters.begin()));
            region.element = std::move(*element);
            region.external = true;
            region.family = source::RepresentationKind::Span;
            return region;
        }
    }
    return std::unexpected("'" + spelled + "' is not a container this body tracks");
}

// The length that bounds a subscript of the object `region` was reached
// through: that object's own, read at its current version (STDMODEL-012).
Expr region_length(const ElementRegion& region, const Locals& locals, CXCursor at) {
    Expr subject;
    if (region.accessed.has_value()) {
        const std::size_t accessed = *region.accessed;
        subject.type = locals[accessed].type;
        subject.location = presumed_location(clang_getCursorLocation(at));
        subject.node = PlaceRef{locals[accessed].version, place_of(locals, accessed)};
    } else if (region.parameter.has_value()) {
        // A span parameter, by its callable position; the region is rooted in
        // its declaration.
        subject.type = convert_type(clang_getCursorType(region.declaration));
        subject.location = presumed_location(clang_getCursorLocation(at));
        subject.node = ParameterRef{*region.parameter, take(clang_getCursorSpelling(region.declaration))};
    } else {
        return unsupported_expression(at, "the storage a subscript reaches has no length");
    }
    Expr length;
    length.type = subject.type.projections.empty() ? Type{} : subject.type.projections.front();
    length.location = subject.location;
    length.node = Projection{0, {std::move(subject)}};
    return length;
}

// The index term a subscript selects, stated at the modeled length type so it
// compares with the bound. A constant is that literal; anything else is the
// term Clang resolved, which must already be of the length type.
std::optional<Expr> element_index(const ResolvedAccess& access, const Type& length, const Signature& signature,
                                  const Locals& locals) {
    if (access.path.empty()) {
        return std::nullopt;
    }
    const PlaceStep& last = access.path.back();
    if (last.kind == PlaceStep::Kind::SymbolicElement) {
        if (access.symbolic_indices.empty()) {
            return std::nullopt;
        }
        return build_expression(access.symbolic_indices.back(), signature, locals, 0);
    }
    Expr literal;
    literal.type = length;
    literal.node = IntLiteral{static_cast<std::int64_t>(last.index)};
    return literal;
}

// The element place of `region` a subscript at `index` names, if this path has
// already formed it at the current generation.
std::optional<std::size_t> find_element(const Locals& locals, const ElementRegion& region,
                                        const std::vector<PlaceStep>& path, const Expr& index) {
    for (std::size_t position = locals.size(); position > 0; --position) {
        const Local& candidate = locals[position - 1];
        if (!candidate.symbolic || candidate.index_value.empty() ||
            clang_equalCursors(candidate.declaration, region.declaration) == 0 || candidate.path != path ||
            !same_term(candidate.index_value.front(), index) || !generation_current(locals, candidate)) {
            continue;
        }
        if (region.root.has_value() != candidate.formed_at.has_value() ||
            (region.root.has_value() && candidate.formed_at->root != *region.root)) {
            continue;
        }
        return position - 1;
    }
    return std::nullopt;
}

Expr library_call(source::LibraryCall library, const Type& container, std::string name, std::vector<Expr> arguments,
                  Type result, CXCursor at) {
    Call call;
    call.callee_usr = library_symbol(library, container);
    call.callee_name = std::move(name);
    call.arguments = std::move(arguments);
    call.library = library;
    Expr expression;
    expression.type = std::move(result);
    expression.location = presumed_location(clang_getCursorLocation(at));
    expression.node = std::move(call);
    return expression;
}

// The argument of `std::move(x)`, when `cursor` is that call: the function
// `move` Clang resolved in namespace `std`, never a spelling.
std::optional<CXCursor> moved_operand_of(CXCursor cursor) {
    cursor = strip_parens(cursor);
    if (clang_getCursorKind(cursor) != CXCursor_CallExpr || clang_Cursor_getNumArguments(cursor) != 1) {
        return std::nullopt;
    }
    const CXCursor callee = clang_getCursorReferenced(cursor);
    if (clang_getCursorKind(callee) != CXCursor_FunctionDecl || take(clang_getCursorSpelling(callee)) != "move") {
        return std::nullopt;
    }
    CXCursor parent = clang_getCursorSemanticParent(callee);
    while (clang_getCursorKind(parent) == CXCursor_Namespace && clang_Cursor_isInlineNamespace(parent) != 0) {
        parent = clang_getCursorSemanticParent(parent);
    }
    if (clang_getCursorKind(parent) != CXCursor_Namespace || take(clang_getCursorSpelling(parent)) != "std" ||
        clang_getCursorKind(clang_getCursorSemanticParent(parent)) != CXCursor_TranslationUnit) {
        return std::nullopt;
    }
    return clang_Cursor_getArgument(cursor, 0);
}

// The root of the tracked vector or string `object` names, directly or through
// a reference, when it names one (RFC 0020 §3). A span is not one: it owns no
// storage a view could be taken of.
std::optional<std::size_t> owning_root(CXCursor object, const Locals& locals) {
    object = strip_parens(object);
    if (clang_getCursorKind(object) != CXCursor_DeclRefExpr) {
        return std::nullopt;
    }
    const auto binding = find_binding(locals, clang_getCursorReferenced(object));
    if (!binding) {
        return std::nullopt;
    }
    const std::size_t storage = locals[*binding].referent.value_or(*binding);
    const Local& root = locals[storage];
    if (!root.sequence.has_value() || root.sequence->views.has_value() || !root.path.empty()) {
        return std::nullopt;
    }
    return storage;
}

bool is_mutator(const SequenceCall& call) {
    return call.name == "push_back" || call.name == "pop_back" || call.name == "clear" || call.name == "reserve" ||
           call.name == "append" || call.name == "operator+=" || call.name == "operator=";
}

// A member call of `std::array` or of a modeled sequence, read as a value
// (RFC 0020 §6). Observations are terms over the current version; element
// access reads the element place the statement formed; mutators and data
// pointers are refused here, because their meaning depends on where they
// stand, which the statement lowering decides.
Expr sequence_expression(const SequenceCall& call, CXCursor cursor, const Signature& signature, const Locals& locals,
                         unsigned depth) {
    using K = source::RepresentationKind;
    const std::string qualified = library_name(call.family, call.name);
    const Type resolved = convert_type(clang_getCursorType(cursor));
    if (call.constructor) {
        const std::vector<CXCursor> copied = parameters_of(call.method);
        // Copying a span copies the view, not the elements: the copy is the
        // same value, and designates the same storage while it lives
        // (SPEC.md J.2).
        if (call.family == K::Span && call.arguments.size() == 1 && copied.size() == 1 &&
            clang_equalCursors(clang_getTypeDeclaration(clang_getCanonicalType(
                                   clang_getPointeeType(clang_getCanonicalType(clang_getCursorType(copied.front()))))),
                               clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(cursor)))) != 0) {
            return build_expression(call.arguments.front(), signature, locals, depth + 1);
        }
        if (call.family == K::Span && call.arguments.size() == 1) {
            return view_argument(call.arguments.front(), resolved, locals, cursor);
        }
        // A copy of a tracked container, or the implicit move of a local a
        // return statement makes, is a container with the same value: the
        // length is all the abstract value holds, and both preserve it
        // (RFC 0020 §6). An explicit `std::move` is not this: it leaves its
        // operand unknown, so it is modeled only where it initializes a local.
        const std::vector<CXCursor> formals = parameters_of(call.method);
        if (source::is_sequence(call.family) && call.arguments.size() == 1 && formals.size() == 1 &&
            !moved_operand_of(call.arguments.front()).has_value()) {
            const CXType formal = clang_getCanonicalType(clang_getCursorType(formals.front()));
            const bool same_class =
                (formal.kind == CXType_LValueReference || formal.kind == CXType_RValueReference) &&
                clang_equalCursors(clang_getTypeDeclaration(clang_getCanonicalType(clang_getPointeeType(formal))),
                                   clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(cursor)))) != 0;
            if (same_class) {
                if (const std::optional<std::size_t> root = owning_root(call.arguments.front(), locals)) {
                    return read_place(locals, *root, cursor);
                }
            }
        }
        return unsupported_expression(cursor, "constructing '" + resolved.spelling +
                                                  "' here is not modeled; a container is constructed as the "
                                                  "initializer of a local (SPEC.md STDMODEL-013)");
    }
    const Type object = convert_type(clang_getCursorType(call.object), 0, ReferenceModel::Referent);
    if (!object.representation.rejection.empty()) {
        return unsupported_expression(cursor,
                                      "'" + object.spelling + "' is not modeled: " + object.representation.rejection);
    }
    const auto literal = [&](std::int64_t value, const Type& type) {
        Expr expression;
        expression.type = type;
        expression.location = presumed_location(clang_getCursorLocation(cursor));
        expression.node = IntLiteral{value};
        return expression;
    };
    if (call.family == K::StdArray) {
        const auto extent = static_cast<std::int64_t>(object.projections.size());
        if ((call.name == "size" || call.name == "max_size") && call.arguments.empty() &&
            resolved.kind == TypeKind::Int) {
            return literal(extent, resolved);
        }
        if (call.name == "empty" && call.arguments.empty() && resolved.kind == TypeKind::Bool) {
            return literal(extent == 0 ? 1 : 0, resolved);
        }
        if (call.name == "operator[]" && call.arguments.size() == 1) {
            // A tracked array's element is its own place; an array held as a
            // value is observed at the index (FOUNDATIONS.md 45).
            if (const auto element = tracked_place(cursor, locals, signature)) {
                return read_place(locals, *element, cursor);
            }
            // An array a reference designates is caller storage another
            // reference may write while the body runs, so it is not read as the
            // value it had on entry (TRUST.md TCB-MEM-005).
            if (const CXCursor named = strip_parens(call.object);
                clang_getCursorKind(named) == CXCursor_DeclRefExpr &&
                source::aliases_storage(passing_of(clang_getCursorType(clang_getCursorReferenced(named))))) {
                return unsupported_expression(cursor, "an element of the std::array a reference designates is not "
                                                      "modeled; take the array by value (SPEC.md STDMODEL-011)");
            }
            Expr subject = build_expression(call.object, signature, locals, depth + 1);
            if (subject.type.representation.kind != K::StdArray) {
                return unsupported_expression(cursor, "this subscript's array is not a value or storage this body "
                                                      "models");
            }
            if (const auto index = constant_index_of(call.arguments.front())) {
                if (*index >= subject.type.projections.size()) {
                    return unsupported_expression(cursor,
                                                  "proof array index must be a constant within the resolved extent");
                }
                Expr projected;
                projected.type = subject.type.projections[*index];
                projected.location = presumed_location(clang_getCursorLocation(cursor));
                projected.node = Projection{*index, {std::move(subject)}};
                return projected;
            }
            return element_observation(std::move(subject), call.arguments.front(), signature, locals, depth, cursor);
        }
        return unsupported_expression(cursor, "'" + qualified +
                                                  "' is not a modeled operation of std::array; size(), empty() and "
                                                  "operator[] are (SPEC.md STDMODEL-011)");
    }
    const bool length = call.name == "size" || (call.family == K::String && call.name == "length");
    if ((length || call.name == "empty") && call.arguments.empty()) {
        Expr subject = build_expression(call.object, signature, locals, depth + 1);
        if (std::holds_alternative<Unsupported>(subject.node)) {
            return subject;
        }
        if (subject.type.representation.kind != call.family || subject.type.projections.size() != 1) {
            return unsupported_expression(cursor, "the length of this container is not a value this body models");
        }
        const Type size = subject.type.projections.front();
        Expr observed;
        observed.type = size;
        observed.location = presumed_location(clang_getCursorLocation(cursor));
        observed.node = Projection{0, {std::move(subject)}};
        if (length) {
            // The modeled length type is what Clang says `size()` returns, or
            // the observation is refused rather than converted.
            if (!same_modeled_value(resolved, size)) {
                return unsupported_expression(cursor, "'" + qualified + "' returns '" + resolved.spelling +
                                                          "', which is not the modeled length type");
            }
            return observed;
        }
        if (resolved.kind != TypeKind::Bool) {
            return unsupported_expression(cursor, "'" + qualified + "' does not return bool");
        }
        Expr empty;
        empty.type = resolved;
        empty.location = observed.location;
        empty.node = Binary{BinaryOp::Equal, {std::move(observed), literal(0, size)}};
        return empty;
    }
    if (call.name == "operator[]" && call.arguments.size() == 1) {
        auto region = element_region(call.object, locals, signature);
        if (!region) {
            return unsupported_expression(cursor, region.error());
        }
        const auto access = resolve_access(cursor);
        const Type size = region_length(*region, locals, cursor).type;
        if (!access) {
            return unsupported_expression(cursor, "this subscript's index could not be resolved");
        }
        const std::optional<Expr> index = element_index(*access, size, signature, locals);
        if (!index) {
            return unsupported_expression(cursor, "this subscript's index could not be resolved");
        }
        if (const auto element = find_element(locals, *region, access->path, *index)) {
            return read_place(locals, *element, cursor);
        }
        return unsupported_expression(cursor, "this element access is not one the statement holding it formed");
    }
    if (call.name == "data") {
        return unsupported_expression(cursor, "'" + qualified +
                                                  "' is modeled only as the argument of a verified call whose "
                                                  "parameter holds a memory capability (SPEC.md STDMODEL-017)");
    }
    if (is_mutator(call)) {
        return unsupported_expression(cursor, "'" + qualified +
                                                  "' is modeled only as a statement of its own, where the storage it "
                                                  "may replace is followed (SPEC.md STDMODEL-013)");
    }
    return unsupported_expression(cursor, "'" + qualified + "' is not a modeled operation of " +
                                              std::string(source::describe_model(call.family)) +
                                              " (SPEC.md STDMODEL-019)");
}

} // namespace cppl::clangbridge::detail
