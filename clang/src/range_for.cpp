#include "access.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "refinements.hpp"
#include "sequences.hpp"
#include "types.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// A range-based `for` in a verified body (SPEC.md LOOP-004, STMT-005,
// STDMODEL-019): the range it iterates, a position the program does not name,
// the element formed at each position where it owes its bound, the loop
// variable initialized from it or bound to it, and the end of each iteration.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::strip_parens;
using bridge::take;

namespace {

// Whether a range-based `for` states an initialization statement before its
// loop variable, `for (init; x : range)`: a `;` in its header outside every
// nested bracket. libclang exposes no cursor for that statement, so it is
// found in the tokens, and a header that cannot be read there -- one a macro
// writes, above all -- counts as stating one.
bool range_for_initializes(CXCursor statement) {
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
    if (count < 2 || take(clang_getTokenSpelling(unit, tokens[0])) != "for" ||
        take(clang_getTokenSpelling(unit, tokens[1])) != "(") {
        return true;
    }
    int nesting = 0;
    for (unsigned index = 1; index < count; ++index) {
        if (clang_getTokenKind(tokens[index]) != CXToken_Punctuation) {
            continue;
        }
        const std::string spelling = take(clang_getTokenSpelling(unit, tokens[index]));
        if (spelling == "(" || spelling == "[" || spelling == "{") {
            ++nesting;
        } else if (spelling == ")" || spelling == "]" || spelling == "}") {
            if (--nesting == 0) {
                return false;
            }
        } else if (spelling == ";" && nesting == 1) {
            return true;
        }
    }
    return true;
}

} // namespace

// A range-based `for` over a range this implementation models (SPEC.md
// LOOP-001, LOOP-004, STMT-005, STDMODEL-019): a vector, a string or a span
// this body names directly, or an array local. Anything else is refused
// naming what it is.
//
// The range is a name, so evaluating it once before the loop has no effect,
// and its length then is its length at every head: an iteration that may
// replace the range's storage and goes on iterating is refused where it
// would go on (`advance_range`), since C++ leaves that undefined. The
// iteration itself is the one loop lowering with a position this body
// names nowhere in place of a written condition and increment.
std::optional<Expr> BodyLowering::lower_range_for(CXCursor statement, const Continuation& next, const Locals& locals,
                                                  unsigned depth) {
    const std::vector<CXCursor> parts = children_of(statement);
    if (parts.size() != 3 || clang_isExpression(clang_getCursorKind(parts[1])) == 0) {
        return reject("the parts of this range-based for could not be resolved");
    }
    if (clang_getCursorKind(parts[0]) != CXCursor_VarDecl) {
        return reject("a range-based for whose loop variable is a structured binding is not modeled");
    }
    if (range_for_initializes(statement)) {
        return reject("a range-based for with an initialization statement before its loop variable is not "
                      "modeled");
    }
    RangeIteration range;
    range.statement = statement;
    range.variable = parts[0];
    const std::string where = describe_location(statement);
    const std::string variable = take(clang_getCursorSpelling(range.variable));

    const CXCursor named = strip_parens(parts[1]);
    if (clang_getCursorKind(named) != CXCursor_DeclRefExpr) {
        return reject("the range of the range-based for at " + where +
                      " is not a name: a range-based for is modeled over a vector, a string or a span this body "
                      "names, or an array local (SPEC.md STDMODEL-019)");
    }
    const CXCursor declaration = clang_getCursorReferenced(named);
    range.range = take(clang_getCursorSpelling(declaration));
    const Type ranged = convert_type(clang_getCursorType(named));
    const source::RepresentationKind family = ranged.representation.kind;
    if (source::is_sequence(family)) {
        auto region = element_region(named, locals, signature);
        if (!region) {
            return reject(region.error());
        }
        range.sequence = true;
        range.region = *region;
        range.element = region->element;
        range.position_type = region_length(*region, locals, statement).type;
        if (region->root.has_value()) {
            range.watched.push_back(*region->root);
        }
        if (region->accessed.has_value() && region->accessed != region->root) {
            range.watched.push_back(*region->accessed);
        }
    } else if (family == source::RepresentationKind::Array || family == source::RepresentationKind::StdArray) {
        // An array's elements are the places a subscript of it forms, as
        // `a[i]` forms them (`symbolic_element_at`): an array local is
        // tracked as one place per element, which gives its extent and its
        // element type as Clang resolved them, and a built-in array a
        // parameter designates has the extent of its declared type. A local
        // reference to an array is another name for storage the places are
        // keyed by, so it is not ranged over.
        if (clang_getCursorKind(declaration) == CXCursor_VarDecl &&
            passing_of(clang_getCursorType(declaration)) != source::ParameterPassing::Value) {
            return reject("the range of the range-based for at " + where + " is '" + range.range +
                          "', a reference to an array; a range-based for is modeled over the array itself");
        }
        const Type* element = nullptr;
        for (const Local& candidate : locals) {
            if (clang_equalCursors(candidate.declaration, declaration) == 0 || candidate.symbolic ||
                candidate.referent.has_value() || candidate.path.size() != 1 ||
                candidate.path.front().kind != PlaceStep::Kind::Element) {
                continue;
            }
            range.extent = std::max<std::int64_t>(range.extent, candidate.path.front().index + 1);
            element = &candidate.type;
        }
        const Type declared = element == nullptr ? declared_place_type(declaration, {}, locals) : Type{};
        if (element == nullptr && declared.representation.kind == source::RepresentationKind::Array &&
            !declared.projections.empty()) {
            range.extent = static_cast<std::int64_t>(declared.projections.size());
            element = &declared.projections.front();
        }
        if (element == nullptr) {
            const CXType held = clang_getArrayElementType(clang_getCanonicalType(clang_getCursorType(named)));
            if (const Type elements = convert_type(held);
                held.kind != CXType_Invalid && elements.kind != TypeKind::Int && elements.kind != TypeKind::Bool) {
                return reject("the elements of '" + range.range + "' are '" + elements.spelling +
                              "', which a range-based for does not bind: an integer, enumeration or Boolean "
                              "element is modeled");
            }
            return reject("array '" + range.range + "', the range of the range-based for at " + where +
                          ", is not storage of this body whose elements a subscript forms places of");
        }
        range.array = declaration;
        range.element = *element;
        range.position_type.kind = TypeKind::Int;
        range.position_type.width = 64;
        range.position_type.is_signed = false;
        range.position_type.spelling = "std::size_t";
    } else {
        return reject("the range of the range-based for at " + where + " is '" + range.range + "' of type '" +
                      ranged.spelling +
                      "', which is not a vector, a string, a span or an array this implementation models "
                      "(SPEC.md STDMODEL-019)");
    }

    // The loop variable: a value initialized from the element, or a
    // reference bound to it (SPEC.md STMT-005).
    const CXType written = clang_getCursorType(range.variable);
    const CXType canonical = clang_getCanonicalType(written);
    range.reference = canonical.kind == CXType_LValueReference || canonical.kind == CXType_RValueReference;
    range.writable = range.reference && clang_isConstQualifiedType(clang_getPointeeType(canonical)) == 0;
    const CXType value_type = range.reference ? reference_value_type(written) : written;
    Type type = convert_type(value_type, 0, ReferenceModel::Opaque, refinements);
    if (type.kind != TypeKind::Int && type.kind != TypeKind::Bool) {
        return reject("loop variable '" + variable + "' has type '" + type.spelling + "', which is not modeled");
    }
    if (refinements != nullptr) {
        auto resolved = refinements_of(range.variable, value_type, *refinements);
        if (!resolved) {
            return reject(resolved.error().message);
        }
        type.refinements = std::move(*resolved);
    }
    range.variable_type = std::move(type);
    if (range.reference && !same_modeled_value(range.variable_type, range.element)) {
        return reject("reference binding changes the modeled value type");
    }
    // An element of a span parameter is reached only under a capability an
    // unsafe block can revoke, which a reference could outlive.
    if (range.reference && range.sequence && !range.region.root.has_value()) {
        return reject("loop variable '" + variable + "' binds an element of span parameter '" + range.range +
                      "'; a reference is bound only to an element of a container this body tracks");
    }

    Locals state = locals;
    Local position;
    position.declaration = statement;
    position.version = next_version++;
    position.type = range.position_type;
    position.spelling = "the position of the range-based for at " + where;
    const std::uint32_t start = position.version;
    state.push_back(std::move(position));
    range.position = state.size() - 1;

    LoopHeader header{statement, clang_getNullCursor(), parts[2], std::nullopt, &next};
    header.range = &range;
    std::optional<Expr> loop = lower_loop(header, state, depth);
    if (!loop) {
        return std::nullopt;
    }
    Expr zero;
    zero.type = range.position_type;
    zero.location = presumed_location(clang_getCursorLocation(statement));
    zero.node = IntLiteral{0};
    return bind(start, place_of(state, range.position), std::move(zero), std::move(*loop), statement,
                range.position_type);
}

// The length a range-based for runs its position up to, read where `locals`
// stand: the range's own, or an array's extent.
Expr BodyLowering::range_length(const RangeIteration& range, const Locals& locals) const {
    if (range.sequence) {
        return region_length(range.region, locals, range.statement);
    }
    Expr extent;
    extent.type = range.position_type;
    extent.location = presumed_location(clang_getCursorLocation(range.statement));
    extent.node = IntLiteral{range.extent};
    return extent;
}

// Whether another iteration of a range-based for runs: its position is
// below the range's length.
Expr BodyLowering::range_condition(const RangeIteration& range, const Locals& locals) const {
    Binary below;
    below.op = BinaryOp::Less;
    below.operands.push_back(read_place(locals, range.position, range.statement));
    below.operands.push_back(range_length(range, locals));
    Expr condition;
    condition.type.kind = TypeKind::Bool;
    condition.type.spelling = "bool";
    condition.location = presumed_location(clang_getCursorLocation(range.statement));
    condition.node = std::move(below);
    return condition;
}

// The measure of a range-based for written without one: the positions
// left. It is checked as any loop measure is, never assumed (SPEC.md
// TERMINATION-004, LOOP-006).
Expr BodyLowering::range_measure(const RangeIteration& range, const Locals& locals) const {
    Binary left;
    left.op = BinaryOp::Sub;
    left.operands.push_back(range_length(range, locals));
    left.operands.push_back(read_place(locals, range.position, range.statement));
    Expr measure;
    measure.type = range.position_type;
    measure.location = presumed_location(clang_getCursorLocation(range.statement));
    measure.node = std::move(left);
    return measure;
}

// The entries an iteration of a range-based for writes beyond what its
// body writes by name: its position, and, through a loop variable bound to
// an element by mutable reference, whatever may be that element.
void BodyLowering::mark_range_writes(const RangeIteration& range, const Locals& locals,
                                     std::vector<bool>& written) const {
    written[range.position] = true;
    if (!range.writable) {
        return;
    }
    Local element;
    element.declaration = range.sequence ? range.region.declaration : range.array;
    element.path = {PlaceStep{PlaceStep::Kind::SymbolicElement, 0, 0}};
    element.external =
        range.sequence ? range.region.external : std::ranges::any_of(locals, [&](const Local& candidate) {
            return candidate.external && clang_equalCursors(candidate.declaration, range.array) != 0;
        });
    element.symbolic = true;
    element.type = range.element;
    for (std::size_t index = 0; index < locals.size(); ++index) {
        if (!locals[index].referent.has_value() && may_alias(element, locals[index])) {
            written[index] = true;
        }
    }
}

// One iteration of a range-based for from its head: the element at the
// position, formed where it owes its bound, the loop variable initialized
// from it or bound to it, then the body (SPEC.md STMT-005).
std::optional<Expr> BodyLowering::lower_range_iteration(const RangeIteration& range, const Continuation& body,
                                                        const Locals& head, unsigned depth) {
    std::vector<Local> enclosing;
    enclosing.swap(formed_derefs);
    std::optional<Expr> lowered = initialize_range_variable(range, body, head, depth);
    if (lowered) {
        lowered = bind_formed_derefs(std::move(*lowered), range.statement);
    }
    formed_derefs = std::move(enclosing);
    return lowered;
}

std::optional<Expr> BodyLowering::initialize_range_variable(const RangeIteration& range, const Continuation& body,
                                                            const Locals& head, unsigned depth) {
    Locals state = head;
    const std::size_t before = state.size();
    Expr index = read_place(state, range.position, range.statement);
    const std::vector<PlaceStep> path{PlaceStep{PlaceStep::Kind::SymbolicElement, 0, 0}};
    std::optional<std::size_t> element;
    if (range.sequence) {
        if (!element_capability(range.region,
                                range.writable ? Capability::Kind::Writable : Capability::Kind::Readable)) {
            return std::nullopt;
        }
        library_models.insert(range.region.family);
        element = sequence_element_at(state, range.region, path, std::move(index), range_length(range, state),
                                      range.range + "[...]");
    } else {
        element = symbolic_element_at(state, range.array, path, std::move(index), false);
    }
    if (!element) {
        return std::nullopt;
    }
    for (std::size_t formed = before; formed < state.size(); ++formed) {
        formed_derefs.push_back(state[formed]);
    }
    const std::string name = take(clang_getCursorSpelling(range.variable));
    Expr value = read_place(state, *element, range.variable);
    const std::uint32_t version = next_version++;
    if (range.reference) {
        Local binding{.declaration = range.variable,
                      .version = version,
                      .type = range.variable_type,
                      .referent = element,
                      .spelling = name};
        binding.borrows = state[*element].formed_at;
        state.push_back(std::move(binding));
    } else {
        if (!same_modeled_value(range.variable_type, value.type)) {
            if (!integral(range.variable_type) || !integral(value.type)) {
                return reject("initializing loop variable '" + name + "' of type '" + range.variable_type.spelling +
                              "' from an element of type '" + value.type.spelling +
                              "' is a conversion that is not modeled");
            }
            value = integral_conversion(std::move(value), range.variable_type, range.variable, false);
        }
        state.push_back(
            Local{.declaration = range.variable, .version = version, .type = range.variable_type, .spelling = name});
    }
    std::optional<Expr> rest = lower_statements(body, state, depth + 1);
    if (!rest) {
        return std::nullopt;
    }
    return bind(version, place_of(state, state.size() - 1), std::move(value), std::move(*rest), range.variable,
                range.variable_type);
}

// The end of an iteration of a range-based for: the storage it iterates
// must be the storage it began with, and the position moves on by one.
//
// C++ took the range's beginning and end before the first iteration, so
// once the range's storage may have been replaced, going on iterating is
// undefined ([stmt.ranged], STDMODEL-015). A path that leaves the loop after
// replacing it, by a `break` or a `return`, never comes here.
std::optional<Expr> BodyLowering::advance_range(const LoopFrame& frame, const Locals& locals, unsigned depth) {
    const RangeIteration& range = *frame.range;
    for (const std::size_t root : range.watched) {
        if (root >= locals.size() || root >= frame.head.size() || locals[root].version == frame.head[root].version) {
            continue;
        }
        std::string why;
        if (const std::optional<Local::Sequence>& held = locals[root].sequence; held.has_value()) {
            why = held->invalidated;
        }
        return reject("the range-based for at " + describe_location(range.statement) + " goes on iterating '" +
                      range.range + "' after " +
                      (why.empty() ? std::string("something that may replace its storage") : why) +
                      "; once the storage a range-based for iterates may have been replaced, C++ leaves the rest "
                      "of the iteration undefined (SPEC.md STDMODEL-015, STDMODEL-019)");
    }
    Locals advanced = locals;
    Expr one;
    one.type = range.position_type;
    one.location = presumed_location(clang_getCursorLocation(range.statement));
    one.node = IntLiteral{1};
    Binary sum;
    sum.op = BinaryOp::Add;
    sum.operands.push_back(read_place(locals, range.position, range.statement));
    sum.operands.push_back(std::move(one));
    Expr next;
    next.type = range.position_type;
    next.location = presumed_location(clang_getCursorLocation(range.statement));
    next.node = std::move(sum);
    const std::uint32_t version = next_version++;
    advanced[range.position].version = version;
    std::optional<Expr> rest = end_iteration(frame, true, advanced, depth + 1);
    if (!rest) {
        return std::nullopt;
    }
    return bind(version, place_of(advanced, range.position), std::move(next), std::move(*rest), range.statement,
                range.position_type);
}

} // namespace cppl::clangbridge::detail
