#include "cppl/clang/bridge.hpp"

#include "access.hpp"
#include "aggregate_values.hpp"
#include "call_objects.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/projection.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
#include "expressions.hpp"
#include "ghost.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "proof_instantiation.hpp"
#include "refinements.hpp"
#include "sequences.hpp"
#include "signature.hpp"
#include "statements.hpp"
#include "types.hpp"
#include "unsafe.hpp"
#include "write_scan.hpp"

#include <algorithm>
#include <clang-c/CXDiagnostic.h>
#include <clang-c/CXErrorCode.h>
#include <clang-c/CXString.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::clangbridge {

namespace detail {

// What libclang reports, read the one way every unit of the bridge reads it
// (places.hpp, cursors.cpp).
using bridge::children_of;
using bridge::presumed_location;
using bridge::record_fields;
using bridge::record_has_base;
using bridge::strip_parens;
using bridge::take;

constexpr std::size_t kMaxReturnPaths = 128;
// A condition's operators nest, and each `&&`/`||` places its second operand on
// a further route, so elaboration is bounded as expression depth is.
constexpr unsigned kMaxConditionDepth = 64;

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

std::string unmodeled_statement(const std::string& found) {
    return "only if/else, while and for loops, blocks, local declarations, assignments, and return statements are "
           "modeled; found " +
           found;
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

std::optional<Expr> BodyLowering::lower_statements(const Continuation& from, const Locals& locals, unsigned depth) {
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
    if (from.dispatch != nullptr) {
        return lower_switch_dispatch(*from.dispatch, locals, depth + 1);
    }
    if (from.left != nullptr) {
        return leave_switch(*from.left, locals, depth + 1);
    }
    if (from.branch != nullptr) {
        return lower_branch(*from.branch, locals, depth + 1);
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
    if (const std::optional<std::string> marker = contradiction_marker(statement)) {
        return lower_contradiction(*marker, *from.statements, from.index, locals);
    }
    if (const std::optional<std::string> marker = split_marker(*from.statements, from.index)) {
        return lower_split(*marker, from, locals, depth);
    }
    if (ghost_marker_of(statement, invariant_prefix)) {
        return lower_ghost(from, locals, depth);
    }
    Continuation next{from.outer, from.statements, from.index + 1};
    next.labels = from.labels;
    // An unsafe block says for itself why a way out of it is refused. A
    // statement a switch's label leads into is reached by that label.
    const bool labelled = next.labels != nullptr && std::ranges::contains(*next.labels, next.index);
    if (next.index != from.statements->size() && !labelled &&
        !unsafe_marker_of(statement, invariant_prefix).has_value() && terminates(statement, 0)) {
        return reject("unreachable trailing statements are not modeled");
    }
    return lower_statement(statement, next, locals, depth);
}

// Lower one statement, then bind every dereference place it formed.
//
// The binding wraps the whole statement's value, so each pointee has an
// entry value before anything reads it. Doing it here rather than in each
// statement form is what keeps a dereference from needing a lowering rule
// of its own (RFC 0014 §17 step 6).
std::optional<Expr> BodyLowering::lower_statement(CXCursor statement, const Continuation& next, const Locals& locals,
                                                  unsigned depth) {
    std::vector<Local> enclosing;
    enclosing.swap(formed_derefs);
    std::optional<Expr> lowered = lower_statement_form(statement, next, locals, depth);
    if (lowered) {
        lowered = bind_formed_derefs(std::move(*lowered), statement);
    }
    formed_derefs = std::move(enclosing);
    return lowered;
}

std::optional<Expr> BodyLowering::lower_statement_form(CXCursor statement, const Continuation& next,
                                                       const Locals& locals, unsigned depth) {
    const CXCursorKind kind = clang_getCursorKind(statement);
    if (const std::optional<CXCursor> marker = unsafe_marker_of(statement, invariant_prefix)) {
        return lower_unsafe(statement, *marker, next, locals, depth);
    }
    if (kind == CXCursor_CompoundStmt) {
        const std::vector<CXCursor> nested = children_of(statement);
        return lower_statements(Continuation{&next, &nested, 0}, locals, depth + 1);
    }
    if (const auto selected = selection_to_split(statement))
        return lower_selected_statement(statement, *selected, next, locals, depth);
    if (kind == CXCursor_CallExpr)
        return lower_call(statement, next, locals, depth);
    // A container mutator whose argument is a temporary stands inside the
    // node Clang adds to destroy that temporary at the statement's end.
    if (kind == CXCursor_UnexposedExpr) {
        const CXCursor inner = strip_parens(statement);
        if (clang_getCursorKind(inner) == CXCursor_CallExpr && sequence_call(inner).has_value()) {
            return lower_call(inner, next, locals, depth);
        }
        // A call whose temporaries are destroyed without running any code
        // of the program's is the call it holds: the cleanup has no effect.
        if (clang_getCursorKind(inner) == CXCursor_CallExpr && temporaries_destroy_silently(statement)) {
            return lower_call(inner, next, locals, depth);
        }
        if (clang_getCursorKind(inner) == CXCursor_CallExpr) {
            return reject("a call statement creates a temporary whose destruction at the statement's end runs a "
                          "user-provided destructor, which is not modeled (SPEC.md STDMODEL-023)");
        }
    }
    if (kind == CXCursor_NullStmt || is_fallthrough(statement))
        return lower_statements(next, locals, depth + 1);
    // `a, b;` as a statement, and as a `for` increment, runs `a` and then
    // `b`, each as a statement of its own (C++ [expr.comma]); `a, b, c` is
    // `(a, b), c`. A comma inside another expression is not this.
    if (kind == CXCursor_BinaryOperator && clang_getCursorBinaryOperatorKind(statement) == CXBinaryOperator_Comma) {
        const std::vector<CXCursor> operands = children_of(statement);
        if (operands.size() != 2) {
            return reject("the operands of this comma operator could not be resolved");
        }
        const std::vector<CXCursor> right{operands[1]};
        const Continuation then{&next, &right, 0};
        return lower_statement(operands[0], then, locals, depth + 1);
    }
    if (kind == CXCursor_SwitchStmt) {
        return lower_switch(statement, next, locals, depth);
    }
    if (kind == CXCursor_ReturnStmt) {
        const std::vector<CXCursor> returned = children_of(statement);
        if (returned.empty() && result_type.kind == TypeKind::Void)
            return completed(void_value(statement), locals, statement);
        if (returned.size() != 1)
            return reject("a return requires one value");
        return lower_returned(returned.front(), statement, locals, depth);
    }
    if (kind == CXCursor_DeclStmt) {
        // The projector puts one declaration in a templated body to make
        // C++ instantiate that specialization's contract probes with it.
        // It names a probe and computes nothing, so it is not a statement
        // of the program being verified and is stepped over rather than
        // modeled (SPEC.md TEMPLATE-001).
        if (is_instantiation_marker(statement)) {
            return lower_statements(next, locals, depth + 1);
        }
        return lower_declaration(children_of(statement), 0, next, locals, depth);
    }
    if (kind == CXCursor_BinaryOperator && clang_getCursorBinaryOperatorKind(statement) == CXBinaryOperator_Assign) {
        return lower_assignment(statement, next, locals, depth);
    }
    if (kind == CXCursor_CompoundAssignOperator || kind == CXCursor_UnaryOperator) {
        return lower_update(statement, next, locals, depth);
    }
    const std::vector<CXCursor> parts = children_of(statement);
    if (kind == CXCursor_IfStmt) {
        return lower_if(statement, next, locals, depth);
    }
    // The condition variable of an `if` or a `switch`, which no other
    // statement list holds.
    if (kind == CXCursor_VarDecl) {
        return lower_declaration(std::vector<CXCursor>{statement}, 0, next, locals, depth);
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
        if (parts.size() != 2 || clang_isExpression(clang_getCursorKind(parts[1])) == 0) {
            return reject("the parts of this do loop could not be resolved");
        }
        const LoopHeader header{statement, parts[1], parts[0], std::nullopt, &next, true};
        return lower_loop(header, locals, depth);
    }
    // A label names the statement it labels and does nothing itself; a
    // `goto` to it is refused where the `goto` stands.
    if (kind == CXCursor_LabelStmt) {
        if (parts.size() != 1 || clang_isStatement(clang_getCursorKind(parts[0])) == 0) {
            return reject("the statement this label names could not be resolved");
        }
        return lower_statement(parts[0], next, locals, depth);
    }
    if (kind == CXCursor_CXXForRangeStmt) {
        return lower_range_for(statement, next, locals, depth);
    }
    return reject(unmodeled_statement(statement_name(kind)));
}

std::optional<Expr> BodyLowering::lower_for(CXCursor statement, const Continuation& next, const Locals& locals,
                                            unsigned depth) {
    const std::optional<ForParts> parts = for_parts(statement);
    if (!parts) {
        return reject("the parts of this for loop could not be resolved");
    }
    if (parts->condition && clang_isExpression(clang_getCursorKind(*parts->condition)) == 0) {
        return reject("a for loop whose condition declares a variable is not modeled");
    }
    // A `for` without a condition runs until a `break` or a `return` leaves
    // it (SPEC.md LOOP-001).
    const LoopHeader header{statement, parts->condition.value_or(clang_getNullCursor()), parts->body, parts->increment,
                            &next};
    if (!parts->initialization) {
        return lower_loop(header, locals, depth);
    }
    // The initialization runs once, before the loop, with the loop as what
    // follows it.
    Continuation entered;
    entered.header = &header;
    return lower_statement(*parts->initialization, entered, locals, depth);
}

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

// The name of the block the projector emitted for a claim that this path
// cannot occur, if `statement` is the declaration that opens one
// (SPEC.md VERIFIED-023).
std::optional<std::string> BodyLowering::contradiction_marker(CXCursor statement) const {
    if (invariant_prefix.empty() || clang_getCursorKind(statement) != CXCursor_DeclStmt) {
        return std::nullopt;
    }
    const std::vector<CXCursor> declared = children_of(statement);
    if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
        return std::nullopt;
    }
    std::string name = take(clang_getCursorSpelling(declared[0]));
    if (!name.starts_with(invariant_prefix + "contradiction_") || name.find("_argument_") != std::string::npos) {
        return std::nullopt;
    }
    return name;
}

// `contradiction evidence;` written here: this path ends. What follows it
// is not lowered, because the claim is that nothing after it is reached; the
// claim itself is the obligation. The evidence's arguments are the block's
// remaining declarations, each read at the versions current here.
std::optional<Expr> BodyLowering::lower_contradiction(const std::string& marker,
                                                      const std::vector<CXCursor>& statements, std::size_t index,
                                                      const Locals& locals) {
    PathContradiction claim;
    claim.marker = marker;
    for (std::size_t position = index + 1; position < statements.size(); ++position) {
        const std::vector<CXCursor> declared = children_of(statements[position]);
        const std::string expected = marker + "_argument_" + std::to_string(position - index - 1);
        if (clang_getCursorKind(statements[position]) != CXCursor_DeclStmt || declared.size() != 1 ||
            clang_getCursorKind(declared[0]) != CXCursor_VarDecl ||
            take(clang_getCursorSpelling(declared[0])) != expected) {
            return reject("the arguments of this contradiction were not resolved");
        }
        const CXCursor initializer = clang_Cursor_getVarDeclInitializer(declared[0]);
        if (clang_Cursor_isNull(initializer) != 0) {
            return reject("an argument of this contradiction was not resolved");
        }
        claim.operands.push_back(build_expression(initializer, signature, locals, 0));
    }
    consumed_contradictions.push_back(marker);
    Expr ended;
    ended.type = result_type;
    ended.location = presumed_location(clang_getCursorLocation(statements[index]));
    ended.node = std::move(claim);
    return ended;
}

// The one variable a generated declaration statement declares, when it
// declares exactly one and it has this name.
std::optional<CXCursor> BodyLowering::declared_as(CXCursor statement, std::string_view name) {
    if (clang_getCursorKind(statement) != CXCursor_DeclStmt) {
        return std::nullopt;
    }
    const std::vector<CXCursor> declared = children_of(statement);
    if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl ||
        take(clang_getCursorSpelling(declared[0])) != name) {
        return std::nullopt;
    }
    return declared[0];
}

// The name of the block the projector emitted for a case split on this
// path, if the statement at `index` opens one: a generated `bool` followed
// by the split's subject (SPEC.md CASE-017).
std::optional<std::string> BodyLowering::split_marker(const std::vector<CXCursor>& statements,
                                                      std::size_t index) const {
    if (invariant_prefix.empty() || index + 1 >= statements.size() ||
        clang_getCursorKind(statements[index]) != CXCursor_DeclStmt) {
        return std::nullopt;
    }
    const std::vector<CXCursor> declared = children_of(statements[index]);
    if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
        return std::nullopt;
    }
    std::string name = take(clang_getCursorSpelling(declared[0]));
    if (!name.starts_with(invariant_prefix + "split_") || !declared_as(statements[index + 1], name + "_subject")) {
        return std::nullopt;
    }
    return name;
}

// A case split written here: the subject is read at the versions current
// here, and each arm continues this path, through its own nested splits and
// claims and then through the rest of the body after the split. Nothing
// about the representation's states is decided here; the arms are carried
// as written, with each arm's binders standing for the values its case
// exposes, and are matched to the partition when the split is elaborated.
std::optional<Expr> BodyLowering::lower_split(const std::string& marker, const Continuation& from, const Locals& locals,
                                              unsigned depth) {
    const std::vector<CXCursor>& statements = *from.statements;
    std::size_t position = from.index + 1;
    const std::optional<CXCursor> subject = declared_as(statements[position++], marker + "_subject");
    const CXCursor value = subject ? clang_Cursor_getVarDeclInitializer(*subject) : clang_getNullCursor();
    if (clang_Cursor_isNull(value) != 0) {
        return reject("the subject of this case split was not resolved");
    }
    CaseSplit split;
    split.marker = marker;
    split.operands.push_back(build_expression(value, signature, locals, 0));
    consumed_splits.push_back(Function::SplitSubject{marker, split.operands.front().type});

    // The request that the subject's type be complete computes nothing.
    while (position < statements.size() && clang_getCursorKind(statements[position]) == CXCursor_DeclStmt &&
           std::ranges::all_of(
               children_of(statements[position]),
               [](CXCursor declared) { return clang_getCursorKind(declared) == CXCursor_StaticAssert; })) {
        ++position;
    }

    std::map<std::uint32_t, std::uint32_t> labels;
    const std::string label_prefix = marker + "_label_";
    for (; position < statements.size() && clang_getCursorKind(statements[position]) == CXCursor_DeclStmt; ++position) {
        const std::vector<CXCursor> declared = children_of(statements[position]);
        const std::string name = declared.size() == 1 ? take(clang_getCursorSpelling(declared[0])) : std::string{};
        const CXCursor label =
            name.starts_with(label_prefix) ? clang_Cursor_getVarDeclInitializer(declared[0]) : clang_getNullCursor();
        const std::string arm = name.substr(std::min(name.size(), label_prefix.size()));
        if (clang_Cursor_isNull(label) != 0 || arm.empty() ||
            arm.find_first_not_of("0123456789") != std::string::npos || arm.size() > 5) {
            return reject("a label of this case split was not resolved");
        }
        labels.emplace(static_cast<std::uint32_t>(std::stoul(arm)), static_cast<std::uint32_t>(split.operands.size()));
        split.operands.push_back(build_expression(label, signature, locals, 0));
    }

    for (std::uint32_t arm = 0; position < statements.size(); ++position, ++arm) {
        if (clang_getCursorKind(statements[position]) != CXCursor_CompoundStmt) {
            return reject("an arm of this case split was not resolved");
        }
        const std::vector<CXCursor> contents = children_of(statements[position]);
        if (contents.empty() || !declared_as(contents[0], marker + "_arm_" + std::to_string(arm))) {
            return reject("an arm of this case split was not resolved");
        }
        // The declarations after the arm's own marker are its binders, in
        // the order they were written; its nested splits and claims are
        // blocks.
        Locals bound = locals;
        std::size_t first = 1;
        std::uint32_t binders = 0;
        for (; first < contents.size() && clang_getCursorKind(contents[first]) == CXCursor_DeclStmt; ++first) {
            const std::vector<CXCursor> declared = children_of(contents[first]);
            if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
                return reject("a binder of this case split was not resolved");
            }
            Local binder;
            binder.declaration = declared[0];
            binder.type = convert_type(clang_getCursorType(declared[0]), 0, ReferenceModel::Referent);
            binder.spelling = take(clang_getCursorSpelling(declared[0]));
            binder.binder = CaseBinder{marker, arm, binders++};
            // A binder names a value of its own, as in a proof body
            // (SPEC.md CASE-006): it never repeats a parameter's name or an
            // enclosing arm's binder.
            const auto repeats = [&binder](const Local& other) {
                return other.binder.has_value() && other.spelling == binder.spelling;
            };
            if (std::ranges::any_of(bound, repeats) || std::ranges::any_of(parameters, [&binder](CXCursor parameter) {
                    return take(clang_getCursorSpelling(parameter)) == binder.spelling;
                })) {
                return reject("case binder '" + binder.spelling + "' duplicates an enclosing value name");
            }
            bound.push_back(std::move(binder));
        }
        split.arms.push_back(CaseSplit::Arm{
            labels.contains(arm) ? std::optional<std::uint32_t>{labels.at(arm)} : std::nullopt, binders});
        std::optional<Expr> continued = lower_statements(Continuation{from.outer, &contents, first}, bound, depth + 1);
        if (!continued) {
            return std::nullopt;
        }
        split.operands.push_back(std::move(*continued));
    }
    if (split.arms.empty() || labels.size() > split.arms.size() ||
        std::ranges::any_of(labels, [&split](const auto& label) { return label.first >= split.arms.size(); })) {
        return reject("this case split was not resolved");
    }

    Expr result;
    result.type = result_type;
    result.location = presumed_location(clang_getCursorLocation(statements[from.index]));
    result.node = std::move(split);
    return result;
}

// Whether this statement is the declaration the projector emitted to force
// a templated function's contract probes to be instantiated alongside it.
//
// Every such declaration is generated, so it is recognized by the
// projector's own prefix, which no ordinary declaration may use.
bool BodyLowering::is_instantiation_marker(CXCursor statement) const {
    if (invariant_prefix.empty() || clang_getCursorKind(statement) != CXCursor_DeclStmt) {
        return false;
    }
    const std::vector<CXCursor> declared = children_of(statement);
    return std::ranges::all_of(
               declared,
               [&](CXCursor candidate) {
                   return clang_getCursorKind(candidate) == CXCursor_VarDecl &&
                          take(clang_getCursorSpelling(candidate)).starts_with(invariant_prefix + "force_");
               }) &&
           !declared.empty();
}

// The generated declaration a loop invariant was projected into, if the
// statement is one.
std::optional<BodyLowering::LoopMarker> BodyLowering::invariant_marker(CXCursor statement) const {
    if (invariant_prefix.empty() || clang_getCursorKind(statement) != CXCursor_DeclStmt) {
        return std::nullopt;
    }
    const std::vector<CXCursor> declared = children_of(statement);
    if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
        return std::nullopt;
    }
    const std::string name = take(clang_getCursorSpelling(declared[0]));
    if (name.starts_with(invariant_prefix + "invariant_")) {
        return LoopMarker{declared[0], false};
    }
    if (name.starts_with(invariant_prefix + "measure_")) {
        return LoopMarker{declared[0], true};
    }
    return std::nullopt;
}

// A loop, as its entry, its head, one iteration, and what follows it
// (SPEC.md 24). Every local the loop writes is carried: from the head on it
// denotes a fresh version, of which only the invariants and the condition
// are known. A local the loop does not write keeps the version it had.
std::optional<Expr> BodyLowering::lower_loop(const LoopHeader& header, const Locals& locals, unsigned depth) {
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
    // A lexicographic measure is one marker per component, in the order
    // written (SPEC.md TERMINATION-004).
    std::vector<CXCursor> measure_markers;
    std::size_t first = 0;
    while (first < statements.size()) {
        const std::optional<LoopMarker> marker = invariant_marker(statements[first]);
        if (!marker) {
            break;
        }
        (marker->measure ? measure_markers : markers).push_back(marker->cursor);
        ++first;
    }

    LoopFrame frame;
    frame.id = next_loop++;
    frame.statement = header.statement;
    frame.head = locals;
    frame.increment = header.increment;
    frame.exit = header.exit;
    frame.frames_outside = frames.size();
    frame.condition = header.condition;
    frame.condition_last = header.condition_last;
    frame.range = header.range;
    std::vector<bool> written(locals.size(), false);
    if (header.range != nullptr) {
        mark_range_writes(*header.range, locals, written);
    }
    if (clang_Cursor_isNull(header.condition) == 0) {
        mark_writes(header.condition, locals, written);
    }
    if (header.increment) {
        mark_writes(*header.increment, locals, written);
    }
    mark_writes(header.body, locals, written);
    if (clang_Cursor_isNull(header.condition) == 0) {
        mark_sequence_writes(header.condition, locals, written);
    }
    if (header.increment) {
        mark_sequence_writes(*header.increment, locals, written);
    }
    mark_sequence_writes(header.body, locals, written);
    // An unsafe block in the loop may write whatever it reaches on any
    // iteration, so each such place is carried: at the head it is a fresh
    // value no fact from before the loop describes (SPEC.md LOOP-005). And
    // an iteration, like what follows the loop, may come after the block,
    // so none of them holds a contract's capability.
    const std::vector<CXCursor> unsafe_inside = unsafe_blocks_in(header.body, invariant_prefix);
    if (!unsafe_inside.empty()) {
        for (const std::size_t index : unsafe_reach(locals)) {
            written[index] = true;
        }
    }
    const std::optional<source::SourceLocation> enclosing_revocation = revoked_by;
    if (!unsafe_inside.empty() && !revoked_by.has_value()) {
        if (const std::optional<CXCursor> marker = unsafe_marker_of(unsafe_inside.front(), invariant_prefix)) {
            revoked_by = presumed_location(clang_getCursorLocation(*marker));
        }
    }
    for (std::size_t index = 0; index < locals.size(); ++index) {
        if (written[index]) {
            frame.carried.push_back(index);
            // A container an iteration may reallocate has, at the head, a
            // generation no view formed before the loop was formed at.
            new_generation(frame.head[index],
                           "the loop at " + describe_location(header.statement) + ", which may change it");
        }
    }

    std::vector<Expr> invariants;
    for (const CXCursor marker : markers) {
        const CXCursor initializer = clang_Cursor_getVarDeclInitializer(marker);
        if (clang_Cursor_isNull(initializer) != 0) {
            return reject("a loop invariant was not resolved");
        }
        // A range-based for's invariant holds at the head, before the loop
        // variable is initialized for the iteration (SPEC.md LOOP-004).
        if (header.range != nullptr &&
            named_declarations(initializer).contains(clang_hashCursor(header.range->variable))) {
            return reject("an invariant of the range-based for at " + describe_location(header.statement) +
                          " names its loop variable '" + take(clang_getCursorSpelling(header.range->variable)) +
                          "', which it holds before: the invariant holds at each iteration's head, before the "
                          "loop variable is initialized (SPEC.md LOOP-004)");
        }
        Expr invariant = build_expression(initializer, signature, frame.head, 0);
        if (!std::holds_alternative<Unsupported>(invariant.node) && invariant.type.kind != TypeKind::Bool) {
            return reject("a loop invariant must be a condition");
        }
        invariants.push_back(std::move(invariant));
        consumed_invariants.push_back(take(clang_getCursorSpelling(marker)));
    }
    // Each measure component is read in the head's scope like an invariant,
    // but it is a value rather than a condition. Its well-founded domain is
    // checked where the obligation is stated (SPEC.md 22.5).
    std::vector<Expr> measures;
    for (const CXCursor marker : measure_markers) {
        const CXCursor initializer = clang_Cursor_getVarDeclInitializer(marker);
        if (clang_Cursor_isNull(initializer) != 0) {
            return reject("a loop measure was not resolved");
        }
        Expr value = build_expression(initializer, signature, frame.head, 0);
        if (!std::holds_alternative<Unsupported>(value.node) && value.type.kind != TypeKind::Int) {
            return reject("a loop measure must be an integer");
        }
        measures.push_back(std::move(value));
        consumed_invariants.push_back(take(clang_getCursorSpelling(marker)));
    }
    // A range-based for written without a measure states the one C++ gives
    // it: the positions left (SPEC.md TERMINATION-004).
    if (header.range != nullptr && measures.empty()) {
        measures.push_back(range_measure(*header.range, frame.head));
    }

    const std::vector<CXCursor> rest(statements.begin() + static_cast<std::ptrdiff_t>(first), statements.end());
    Continuation iteration;
    iteration.iteration = &frame;
    // One iteration from the head, and what follows the loop.
    const Branch iterate = [&](const Locals& state) {
        frames.push_back(&frame);
        std::optional<Expr> once =
            header.range != nullptr
                ? lower_range_iteration(*header.range, Continuation{&iteration, &rest, 0}, state, depth + 1)
                : lower_statements(Continuation{&iteration, &rest, 0}, state, depth + 1);
        frames.pop_back();
        return once;
    };
    const Branch leave = [&](const Locals& state) {
        return lower_statements(*header.exit, state, depth + 1);
    };

    // What happens from the head on. A `do` loop runs its body first and
    // decides at each iteration's end; a `for` without a condition always
    // runs it, and is left only by a `break` or a `return` (SPEC.md
    // LOOP-001). Otherwise the condition decides before each iteration,
    // split into the routes it selects as an `if`'s is.
    std::optional<Expr> decided;
    if (header.condition_last || (clang_Cursor_isNull(header.condition) != 0 && header.range == nullptr)) {
        decided = iterate(frame.head);
    } else if (header.range == nullptr) {
        decided = lower_condition(header.condition, iterate, leave, frame.head, depth + 1);
    } else if (std::optional<Expr> once = iterate(frame.head); once) {
        if (std::optional<Expr> after = leave(frame.head); after) {
            decided.emplace();
            decided->type = result_type;
            decided->node =
                Conditional{{range_condition(*header.range, frame.head), std::move(*once), std::move(*after)}};
        }
    }
    revoked_by = enclosing_revocation;
    if (!decided) {
        return std::nullopt;
    }
    if (return_paths(*decided) > kMaxReturnPaths) {
        return reject("more than " + std::to_string(kMaxReturnPaths) + " return paths are not modeled");
    }
    const source::SourceLocation location = presumed_location(clang_getCursorLocation(header.statement));
    Expr head = std::move(*decided);
    head.location = location;

    Loop loop;
    loop.loop = frame.id;
    for (const std::size_t index : frame.carried) {
        loop.heads.push_back(frame.head[index].version);
        loop.places.push_back(place_of(locals, index));
        loop.operands.push_back(read_place(locals, index, header.statement));
    }
    loop.invariants = static_cast<std::uint32_t>(invariants.size());
    for (Expr& invariant : invariants) {
        loop.operands.push_back(std::move(invariant));
    }
    loop.measures = static_cast<std::uint32_t>(measures.size());
    for (Expr& measure : measures) {
        loop.operands.push_back(std::move(measure));
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
std::optional<Expr> BodyLowering::end_iteration(const LoopFrame& frame, bool after_increment, const Locals& locals,
                                                unsigned depth) {
    if (frame.range != nullptr && !after_increment) {
        return advance_range(frame, locals, depth);
    }
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
            next.operands.push_back(read_place(locals, index, frame.statement));
        }
    }
    Expr iterated;
    iterated.type = result_type;
    iterated.location = presumed_location(clang_getCursorLocation(frame.statement));
    iterated.node = std::move(next);
    if (!frame.condition_last) {
        return iterated;
    }
    // A `do` loop decides here, where its body ends or a `continue` leaves
    // it, whether another iteration begins; when not, what follows the loop
    // runs under the versions current here and outside the loop.
    const Branch again = [&](const Locals&) -> std::optional<Expr> {
        return iterated;
    };
    const Branch leave = [&](const Locals& state) {
        const std::vector<const LoopFrame*> inside = frames;
        const std::vector<const SwitchFrame*> switches = switch_frames;
        frames.resize(frame.frames_outside);
        leave_switches_inside(frame);
        std::optional<Expr> after = lower_statements(*frame.exit, state, depth + 1);
        frames = inside;
        switch_frames = switches;
        return after;
    };
    return lower_condition(frame.condition, again, leave, locals, depth + 1);
}

// `break` continues with what follows the innermost loop or switch, under
// the versions current here, and outside it. A switch is the innermost when
// no loop was entered after it.
std::optional<Expr> BodyLowering::lower_break(const Locals& locals, unsigned depth) {
    if (!switch_frames.empty() && switch_frames.back()->loops_outside == frames.size()) {
        return leave_switch(*switch_frames.back(), locals, depth);
    }
    if (frames.empty()) {
        return reject("'break' outside a modeled loop or switch");
    }
    // No switch entered inside this loop is still open here: a `break`
    // inside one belongs to it.
    const LoopFrame& frame = *frames.back();
    const std::vector<const LoopFrame*> inside = frames;
    frames.resize(frame.frames_outside);
    std::optional<Expr> after = lower_statements(*frame.exit, locals, depth + 1);
    frames = inside;
    return after;
}

// Forgets the switches entered inside `frame`'s loop, for what follows the
// loop, where a `break` cannot belong to them. A `continue` in such a
// switch reaches the end of an iteration with the switch still open.
void BodyLowering::leave_switches_inside(const LoopFrame& frame) {
    while (!switch_frames.empty() && switch_frames.back()->loops_outside > frame.frames_outside) {
        switch_frames.pop_back();
    }
}

// A `switch` statement (C++ [stmt.switch]). Its condition is evaluated
// once, before any comparison, and that one value is compared with each
// case value in the order the labels appear: control enters the body at the
// first label whose value it equals, or else at `default:`, or else goes on
// with what follows the switch. From where it enters, the body runs to its
// end, through every later label, unless a `break`, `return` or `continue`
// leaves it first.
std::optional<Expr> BodyLowering::lower_switch(CXCursor statement, const Continuation& next, const Locals& locals,
                                               unsigned depth) {
    // libclang lists no init-statement among a switch's children, so one
    // left unseen would be a statement the program runs and the model
    // drops.
    const std::optional<SelectionHead> head = selection_head(statement);
    if (!head.has_value()) {
        return reject("the head of this 'switch' statement could not be read, so whether it holds an "
                      "init-statement is not known");
    }
    if (head->separator.has_value()) {
        return reject("a 'switch' statement with an init-statement is not modeled");
    }
    // The condition and the body, after the condition variable if one is
    // declared. Whatever the head holds starts where its parentheses
    // open: an init-statement no token shows, written through a macro,
    // would put the condition later.
    const std::vector<CXCursor> parts = children_of(statement);
    const bool declares = parts.size() == 3 && clang_getCursorKind(parts[0]) == CXCursor_VarDecl;
    const FilePosition opening = start_of(parts.front());
    if ((parts.size() != 2 && !declares) || clang_isExpression(clang_getCursorKind(parts[parts.size() - 2])) == 0 ||
        !stands_at(opening, head->file, head->first)) {
        return reject("the parts of this 'switch' statement could not be resolved");
    }
    const SwitchHeader header{statement, parts[parts.size() - 2], parts.back(), &next};
    if (!declares) {
        return lower_switch_dispatch(header, locals, depth);
    }
    // A condition variable is a local the condition's initializer
    // initializes, in scope through the whole body, and the condition is a
    // read of it (C++ [stmt.pre]).
    const std::vector<CXCursor> variable{parts[0]};
    Continuation dispatched;
    dispatched.dispatch = &header;
    return lower_declaration(variable, 0, dispatched, locals, depth);
}

// One `case` value, as a literal of the condition's type. It is a
// converted constant expression of that type (C++ [stmt.switch]), so Clang
// has converted it without narrowing and its value is one of the type's;
// what is read here is the value Clang evaluates, never one recomputed.
std::optional<Expr> BodyLowering::case_value(CXCursor value, const Type& type) {
    CXEvalResult evaluated = clang_Cursor_Evaluate(value);
    const bool integer = evaluated != nullptr && clang_EvalResult_getKind(evaluated) == CXEval_Int;
    const bool is_unsigned = integer && clang_EvalResult_isUnsignedInt(evaluated) != 0;
    const unsigned long long magnitude = is_unsigned ? clang_EvalResult_getAsUnsigned(evaluated) : 0;
    const long long signed_value = integer && !is_unsigned ? clang_EvalResult_getAsLongLong(evaluated) : 0;
    if (evaluated != nullptr) {
        clang_EvalResult_dispose(evaluated);
    }
    if (!integer) {
        return reject("a 'case' value Clang does not evaluate to an integer is not modeled");
    }
    Expr literal;
    literal.type = type;
    literal.location = presumed_location(clang_getCursorLocation(value));
    literal.node = IntLiteral{is_unsigned ? static_cast<std::int64_t>(magnitude) : signed_value};
    return literal;
}

std::optional<Expr> BodyLowering::lower_switch_dispatch(const SwitchHeader& header, const Locals& locals,
                                                        unsigned depth) {
    // The body's statements with every label taken off them, in order,
    // and the labels, each with the position it leads into. A label's
    // statement is the one it is written on; the statements after it in
    // the body follow it.
    struct Entry {
        CXCursor label;
        std::optional<CXCursor> value; // none for `default:`
        std::size_t position = 0;
    };
    std::vector<CXCursor> written;
    if (clang_getCursorKind(header.body) == CXCursor_CompoundStmt) {
        written = children_of(header.body);
    } else {
        written.push_back(header.body);
    }
    std::vector<Entry> entries;
    std::vector<CXCursor> statements;
    std::vector<std::size_t> positions;
    for (CXCursor statement : written) {
        if (entries.empty() && !is_switch_label(statement)) {
            return reject("a statement before the first label of a 'switch' is never executed, and is not "
                          "modeled");
        }
        while (is_switch_label(statement)) {
            const std::vector<CXCursor> label = children_of(statement);
            const bool fallback = clang_getCursorKind(statement) == CXCursor_DefaultStmt;
            if (!fallback && label.size() == 3) {
                return reject("a case range, 'case low ... high:', is not modeled");
            }
            if (label.size() != (fallback ? 1U : 2U)) {
                return reject("a label of this 'switch' could not be resolved");
            }
            entries.push_back(
                Entry{statement, fallback ? std::nullopt : std::optional<CXCursor>{label.front()}, statements.size()});
            positions.push_back(statements.size());
            statement = label.back();
        }
        // A label anywhere else is a way into the middle of a statement
        // that lowering it from its start never takes (Duff's device).
        if (holds_switch_label(statement)) {
            return reject("a 'case' or 'default' label inside a nested statement of its 'switch' is not modeled");
        }
        statements.push_back(statement);
    }

    // The condition, evaluated once, with any call in it and that call's
    // effects, and bound to one version every comparison reads.
    Locals state = locals;
    std::vector<std::size_t> invalidated;
    std::optional<Expr> value = evaluate(header.condition, state, invalidated);
    if (!value) {
        return std::nullopt;
    }
    const Type condition = value->type;
    const std::uint32_t version = next_version++;
    std::vector<std::optional<Expr>> literals;
    for (const Entry& entry : entries) {
        if (!entry.value.has_value()) {
            literals.emplace_back();
            continue;
        }
        std::optional<Expr> literal = case_value(*entry.value, condition);
        if (!literal) {
            return std::nullopt;
        }
        literals.push_back(std::move(literal));
    }

    // Each way into the body, lowered from its label, inside the switch.
    SwitchFrame frame{header.exit, frames.size(), switch_frames.size()};
    Continuation leave;
    leave.left = &frame;
    switch_frames.push_back(&frame);
    std::vector<Expr> entered;
    for (const Entry& entry : entries) {
        Continuation from{&leave, &statements, entry.position};
        from.labels = &positions;
        std::optional<Expr> lowered = lower_statements(from, state, depth + 1);
        if (!lowered) {
            switch_frames.pop_back();
            return std::nullopt;
        }
        entered.push_back(std::move(*lowered));
    }
    switch_frames.pop_back();

    // No value matches: `default:`, or else what follows the switch.
    const auto fallback = std::ranges::find_if(entries, [](const Entry& entry) { return !entry.value.has_value(); });
    std::optional<Expr> chain;
    if (fallback != entries.end()) {
        chain = std::move(entered[static_cast<std::size_t>(fallback - entries.begin())]);
    } else {
        chain = lower_statements(*header.exit, state, depth + 1);
    }
    if (!chain) {
        return std::nullopt;
    }
    Type truth;
    truth.kind = TypeKind::Bool;
    truth.spelling = "bool";
    for (std::size_t index = entries.size(); index-- > 0;) {
        std::optional<Expr>& literal = literals[index];
        if (!literal.has_value()) {
            continue;
        }
        if (return_paths(entered[index]) + return_paths(*chain) > kMaxReturnPaths) {
            return reject("more than " + std::to_string(kMaxReturnPaths) + " return paths are not modeled");
        }
        const source::SourceLocation at = presumed_location(clang_getCursorLocation(entries[index].label));
        Expr read;
        read.type = condition;
        read.location = at;
        read.node = PlaceRef{version, anonymous_place("switch condition")};
        Expr matches;
        matches.type = truth;
        matches.location = at;
        matches.node = Binary{BinaryOp::Equal, {std::move(read), std::move(*literal)}};
        Expr branch;
        branch.type = chain->type;
        branch.location = at;
        branch.node = Conditional{{std::move(matches), std::move(entered[index]), std::move(*chain)}};
        chain = std::move(branch);
    }
    Expr body = std::move(*chain);
    for (const std::size_t changed : invalidated) {
        body = unknown(state, changed, std::move(body), header.statement);
    }
    return bind(version, anonymous_place("switch condition"), std::move(*value), std::move(body), header.statement);
}

// What follows a switch, reached by a `break` belonging to it or by the
// end of its body, lowered outside the switch: a `break` there belongs to
// whatever encloses the switch.
std::optional<Expr> BodyLowering::leave_switch(const SwitchFrame& frame, const Locals& locals, unsigned depth) {
    const std::vector<const SwitchFrame*> inside = switch_frames;
    switch_frames.resize(std::min(switch_frames.size(), frame.switches_outside));
    std::optional<Expr> after = lower_statements(*frame.exit, locals, depth + 1);
    switch_frames = inside;
    return after;
}

// Elaborate an `if` condition into the routes it selects between.
//
// `&&` and `||` state a proposition, and a proposition is not a value: the
// core computes no Boolean from one (SPEC.md 12.7). They are not lowered as
// values here either. They are elaborated into the branch structure C++
// already gives them, which is what makes short-circuit evaluation exact
// rather than approximated:
//
//     if (A && B) T else F   ==>   if (A) { if (B) T else F } else F
//     if (A || B) T else F   ==>   if (A) T else { if (B) T else F }
//     if (!A)     T else F   ==>   if (A) F else T
//
// `B` appears only under the route on which C++ evaluates it, so no route
// can state a fact about an operand that did not execute on it. The false
// route of `A && B` is the union of `!A` and `A && !B`; it is represented as
// those two routes, never as a single route supposing both operands false.
// Nesting recurses, so each operand is itself elaborated the same way.
std::optional<Expr> BodyLowering::lower_condition(CXCursor condition, const Branch& when_true, const Branch& when_false,
                                                  const Locals& locals, unsigned depth) {
    if (depth > kMaxConditionDepth) {
        return reject("this condition nests more deeply than " + std::to_string(kMaxConditionDepth) + " operators");
    }
    const enum CXCursorKind kind = clang_getCursorKind(condition);
    if (kind == CXCursor_ParenExpr) {
        const auto inner = children_of(condition);
        if (inner.size() == 1)
            return lower_condition(inner[0], when_true, when_false, locals, depth + 1);
    }
    if (kind == CXCursor_UnaryOperator && clang_getCursorUnaryOperatorKind(condition) == CXUnaryOperator_LNot) {
        const auto operands = children_of(condition);
        if (operands.size() == 1)
            return lower_condition(operands[0], when_false, when_true, locals, depth + 1);
    }
    if (kind == CXCursor_BinaryOperator) {
        const enum CXBinaryOperatorKind op = clang_getCursorBinaryOperatorKind(condition);
        const auto operands = children_of(condition);
        if ((op == CXBinaryOperator_LAnd || op == CXBinaryOperator_LOr) && operands.size() == 2) {
            const bool conjunction = op == CXBinaryOperator_LAnd;
            // The second operand is evaluated only on the route the first
            // operand's outcome leads to, which is where it is placed.
            const Branch rest = [&](const Locals& state) -> std::optional<Expr> {
                return lower_condition(operands[1], when_true, when_false, state, depth + 1);
            };
            return lower_condition(operands[0], conjunction ? rest : when_true, conjunction ? when_false : rest, locals,
                                   depth + 1);
        }
    }
    // What the leaf reads through a subscript or a pointer is formed where the
    // leaf is evaluated, and owes its bound or capability on the routes that
    // reach it and nowhere else.
    return forming(condition, [&]() -> std::optional<Expr> {
        Locals state = locals;
        if (!signature.clause && !form_places(condition, state))
            return std::nullopt;
        Expr value = runtime_value(build_expression(condition, signature, state, 0), signature.clause);
        std::optional<Expr> taken = when_true(state);
        std::optional<Expr> untaken = taken ? when_false(state) : std::nullopt;
        if (!taken || !untaken)
            return std::nullopt;
        if (return_paths(*taken) + return_paths(*untaken) > kMaxReturnPaths)
            return reject("more than " + std::to_string(kMaxReturnPaths) + " return paths are not modeled");
        Expr result;
        result.type = taken->type;
        result.location = value.location;
        result.node = Conditional{{std::move(value), std::move(*taken), std::move(*untaken)}};
        return result;
    });
}

// An `if` statement (C++ [stmt.if]). An init-statement runs first, in a
// scope enclosing the whole statement, so what it declares is visible in
// the condition and in both branches and ends after them; a condition
// variable is a local its initializer initializes, and the condition reads
// it. Which child is which is decided by where each stands against the
// head's parentheses: the parts written in them come first, and what
// follows them is the branches.
std::optional<Expr> BodyLowering::lower_if(CXCursor statement, const Continuation& next, const Locals& locals,
                                           unsigned depth) {
    const std::optional<SelectionHead> head = selection_head(statement);
    if (!head.has_value()) {
        return reject("the head of this 'if' statement could not be read");
    }
    if (head->immediate) {
        return reject("an 'if consteval' statement is not modeled: which branch runs depends on whether the "
                      "evaluation is a constant one, and a contract describes the function as it runs");
    }
    const std::vector<CXCursor> parts = children_of(statement);
    std::size_t inside = 0;
    while (inside < parts.size() && before(start_of(parts[inside]), head->file, head->close)) {
        ++inside;
    }
    const std::size_t branches = parts.size() - inside;
    if (inside == 0 || (branches != 1 && branches != 2) || !stands_at(start_of(parts[0]), head->file, head->first)) {
        return reject("the parts of this 'if' statement could not be resolved");
    }
    // The parts in the parentheses: the init-statement, which ends before
    // the head's `;`, then a condition variable, then the condition.
    std::vector<CXCursor> prefix;
    std::size_t condition = 0;
    if (head->separator.has_value() && before(start_of(parts[0]), head->file, *head->separator)) {
        prefix.push_back(parts[0]);
        condition = 1;
    }
    if (condition < inside && clang_getCursorKind(parts[condition]) == CXCursor_VarDecl) {
        prefix.push_back(parts[condition]);
        ++condition;
    }
    // An init-statement no `;` in the head shows, written through a macro,
    // is not read as the condition.
    if (condition + 1 != inside || clang_isExpression(clang_getCursorKind(parts[condition])) == 0) {
        return reject("the parts of this 'if' statement could not be resolved");
    }
    const IfHeader header{statement,
                          std::vector<CXCursor>(parts.begin() + static_cast<std::ptrdiff_t>(condition), parts.end()),
                          &next, head->constant};
    if (prefix.empty()) {
        return lower_branch(header, locals, depth);
    }
    Continuation decided;
    decided.branch = &header;
    return lower_statements(Continuation{&decided, &prefix, 0}, locals, depth + 1);
}

// The branches of an `if`. Those of `if constexpr` are selected by a
// constant condition Clang evaluates, and only the selected one runs: in a
// template the other is not even instantiated.
std::optional<Expr> BodyLowering::lower_branch(const IfHeader& header, const Locals& locals, unsigned depth) {
    if (!header.constant) {
        return lower_branch(header.statement, header.parts, *header.exit, locals, depth);
    }
    CXEvalResult evaluated = clang_Cursor_Evaluate(header.parts[0]);
    const bool known = evaluated != nullptr && clang_EvalResult_getKind(evaluated) == CXEval_Int;
    const bool holds = known && clang_EvalResult_getAsLongLong(evaluated) != 0;
    if (evaluated != nullptr) {
        clang_EvalResult_dispose(evaluated);
    }
    if (!known) {
        return reject("the condition of this 'if constexpr' is not a constant Clang evaluates");
    }
    if (holds) {
        return lower_statement(header.parts[1], *header.exit, locals, depth + 1);
    }
    if (header.parts.size() == 3) {
        return lower_statement(header.parts[2], *header.exit, locals, depth + 1);
    }
    return lower_statements(*header.exit, locals, depth + 1);
}

std::optional<Expr> BodyLowering::lower_branch(CXCursor statement, const std::vector<CXCursor>& parts,
                                               const Continuation& next, const Locals& locals, unsigned depth) {
    const Branch when_true = [&](const Locals& state) -> std::optional<Expr> {
        return lower_statement(parts[1], next, state, depth + 1);
    };
    const Branch when_false = [&](const Locals& state) -> std::optional<Expr> {
        return parts.size() == 3 ? lower_statement(parts[2], next, state, depth + 1)
                                 : lower_statements(next, state, depth + 1);
    };
    std::optional<Expr> result = lower_condition(parts[0], when_true, when_false, locals, depth);
    if (!result)
        return std::nullopt;
    result->location = presumed_location(clang_getCursorLocation(statement));
    return result;
}

// The scalar places an aggregate initializer establishes, in declaration
// order, following members that are themselves aggregates into their own
// members (SPEC.md 12.10).
//
// A nested member is not one value: it is the places its own members are,
// reached by a longer path. `s.i.v` and `s.items[0]` are places exactly as
// `s.a` is, which is why this collects leaves rather than stopping at the
// first structural member. Returns the reason on refusal.
std::optional<std::string> BodyLowering::collect_leaves(const Type& type, CXCursor initializer,
                                                        const std::string& written,
                                                        const std::vector<PlaceStep>& prefix,
                                                        std::vector<AggregateLeaf>& leaves) {
    const auto& components = type.representation.components;
    // `std::array<T, N>` is `N` element places exactly as `T[N]` is
    // (RFC 0020 §3, SPEC.md STDMODEL-011).
    const bool array = type.representation.kind == source::RepresentationKind::Array ||
                       type.representation.kind == source::RepresentationKind::StdArray;
    if (type.representation.kind == source::RepresentationKind::StdArray) {
        library_models.insert(source::RepresentationKind::StdArray);
    }
    if (const std::string& unmodeled = type.representation.rejection; !unmodeled.empty()) {
        return "'" + written + "' has type '" + type.spelling + "', which is not modeled: " + unmodeled;
    }
    if ((type.representation.kind != source::RepresentationKind::Record && !array) || components.empty() ||
        type.projections.size() != components.size()) {
        return "'" + written + "' has type '" + type.spelling + "', which is not modeled";
    }
    if (prefix.size() >= kMaxPlaceDepth) {
        return "'" + written + "' nests deeper than this implementation tracks";
    }
    for (const auto& component : components) {
        if (!component.accessible) {
            return "'" + written + "' has type '" + type.spelling +
                   "' with an inaccessible member, whose construction this body cannot check";
        }
    }
    // Only a form whose effect on every member is visible here can be
    // tracked. Default initialization, a constructor call and any other
    // form leave at least one member holding a value this body cannot
    // state, and a tracked member at an unconstrained value would read as
    // though it held one. That applies at every level, so a nested member
    // needs its own braces rather than an elided initializer.
    const CXCursor list = aggregates::braced_list(initializer);
    if (clang_Cursor_isNull(list) != 0) {
        return "'" + written + "' of type '" + type.spelling +
               "' is not initialized by an aggregate initializer, so this body cannot state what each member holds";
    }
    const std::vector<CXCursor> elements = children_of(list);
    if (elements.size() != components.size()) {
        return "'" + written + "' of type '" + type.spelling + "' is initialized with " +
               std::to_string(elements.size()) + " values for " + std::to_string(components.size()) +
               " members; partial aggregate initialization is not modeled";
    }
    for (std::size_t member = 0; member < components.size(); ++member) {
        if (leaves.size() >= kMaxTrackedLeaves) {
            return "'" + written + "' has more tracked members than the proof resource limit allows";
        }
        const Type& member_type = type.projections[member];
        const std::string member_written =
            array ? written + "[" + components[member].name + "]" : written + "." + components[member].name;
        std::vector<PlaceStep> path = prefix;
        path.push_back(
            PlaceStep{array ? PlaceStep::Kind::Element : PlaceStep::Kind::Field, static_cast<std::uint32_t>(member)});
        if (member_type.kind == TypeKind::Value) {
            if (auto refusal = collect_leaves(member_type, elements[member], member_written, path, leaves)) {
                return refusal;
            }
            continue;
        }
        if (member_type.kind == TypeKind::Unsupported) {
            return "member '" + member_written + "' has type '" + member_type.spelling + "', which is not modeled";
        }
        leaves.push_back(AggregateLeaf{std::move(path), member_type, elements[member], member_written});
    }
    return std::nullopt;
}

// The scalar places a value of `type` occupies, in declaration order, with
// no initializer to supply them.
//
// A by-value parameter arrives already holding a value the caller
// established, so what is enumerated here is where that value lives rather
// than how it was built -- which is the whole difference from
// `collect_leaves`. The structural rules are otherwise the same: a member
// that is itself an aggregate is followed into its own members, and a type
// this implementation does not model is refused rather than tracked, since
// an untracked member would read as an unconstrained value while still
// carrying its declared refinement.
std::optional<std::string> BodyLowering::collect_type_leaves(const Type& type, const std::string& written,
                                                             const std::vector<PlaceStep>& prefix,
                                                             std::vector<AggregateLeaf>& leaves) {
    const auto& components = type.representation.components;
    const bool array = type.representation.kind == source::RepresentationKind::Array ||
                       type.representation.kind == source::RepresentationKind::StdArray;
    if ((type.representation.kind != source::RepresentationKind::Record && !array) || components.empty() ||
        type.projections.size() != components.size()) {
        return "'" + written + "' has type '" + type.spelling + "', which is not modeled";
    }
    // A member the representation could not model is left out of its
    // components, so the components no longer stand at the positions
    // `field_index_of` numbers members by: the place of `s.x` would be
    // tracked under the number an access to the member before it resolves
    // to. Such a type is not tracked at all, rather than tracked with every
    // member after the gap under another member's name.
    if (const std::string& unmodeled = type.representation.rejection; !unmodeled.empty()) {
        return "'" + written + "' has type '" + type.spelling + "', which is not modeled: " + unmodeled;
    }
    if (prefix.size() >= kMaxPlaceDepth) {
        return "'" + written + "' nests deeper than this implementation tracks";
    }
    for (const auto& component : components) {
        if (!component.accessible) {
            return "'" + written + "' has type '" + type.spelling +
                   "' with an inaccessible member, whose value this body cannot state";
        }
    }
    for (std::size_t member = 0; member < components.size(); ++member) {
        if (leaves.size() >= kMaxTrackedLeaves) {
            return "'" + written + "' has more tracked members than the proof resource limit allows";
        }
        const Type& member_type = type.projections[member];
        const std::string member_written =
            array ? written + "[" + components[member].name + "]" : written + "." + components[member].name;
        std::vector<PlaceStep> path = prefix;
        path.push_back(
            PlaceStep{array ? PlaceStep::Kind::Element : PlaceStep::Kind::Field, static_cast<std::uint32_t>(member)});
        if (member_type.kind == TypeKind::Value) {
            if (auto refusal = collect_type_leaves(member_type, member_written, path, leaves)) {
                return refusal;
            }
            continue;
        }
        if (member_type.kind == TypeKind::Unsupported) {
            return "member '" + member_written + "' has type '" + member_type.spelling + "', which is not modeled";
        }
        leaves.push_back(AggregateLeaf{std::move(path), member_type, clang_getNullCursor(), member_written});
    }
    return std::nullopt;
}

// An aggregate local, tracked as one place per data member (SPEC.md 12.10).
//
// Each member is bound to the value its initializer supplies, at the member's
// own declared type, so a refined member owes its predicate here exactly as a
// refined local does. That is what makes `S{-5}` a proof obligation rather
// than a fact: the crossing happens at construction, where the value is
// known, instead of being supplied on a later read.
//
// Only a form whose construction is fully visible is admitted. Anything else
// is refused rather than tracked, because an untracked member would read as
// an unconstrained value while still carrying its declared refinement.
std::optional<Expr> BodyLowering::lower_aggregate(CXCursor declaration, const std::string& name, const Type& type,
                                                  const std::vector<CXCursor>& declared, std::size_t index,
                                                  const Continuation& next, const Locals& locals, unsigned depth) {
    // A local initialized from a whole value of its type -- a copy or a
    // move of another object, or a call's result -- takes each member from
    // that value rather than from an initializer per member (TRUST.md
    // TCB-AGGREGATE-001).
    const CXCursor initializer = clang_Cursor_getVarDeclInitializer(declaration);
    if (aggregates::initializes_whole(initializer)) {
        StructHooks hooks(*this);
        return aggregates::lower_initialization(
            hooks, declaration, name, type, initializer, locals,
            [&](const Locals& declaring) { return lower_declaration(declared, index + 1, next, declaring, depth); });
    }
    // An array is a record whose members are its elements, so a constant
    // index names a place exactly as a field name does. A variable index
    // does not: which place it names is not decided here, and deciding it
    // needs the extent obligation the capability model supplies.
    std::vector<AggregateLeaf> leaves;
    if (auto refusal = collect_leaves(type, initializer, name, {}, leaves)) {
        return reject("local " + *refusal);
    }

    Locals declaring = locals;
    std::vector<std::uint32_t> versions;
    std::vector<Expr> values;
    for (const AggregateLeaf& leaf : leaves) {
        std::vector<std::size_t> invalidated;
        auto evaluated = evaluate(leaf.initializer, declaring, invalidated);
        if (!evaluated)
            return std::nullopt;
        if (!invalidated.empty())
            return reject("initializing '" + leaf.spelling +
                          "' has uncertain aliases; use a separate call "
                          "statement");
        if (!std::holds_alternative<Unsupported>(evaluated->node) && !same_modeled_value(leaf.type, evaluated->type)) {
            return reject("initializing '" + leaf.spelling + "' of type '" + leaf.type.spelling + "' from '" +
                          evaluated->type.spelling + "' is a conversion that is not modeled");
        }
        versions.push_back(next_version++);
        values.push_back(std::move(*evaluated));
        declaring.push_back(Local{.declaration = declaration,
                                  .version = versions.back(),
                                  .type = leaf.type,
                                  .path = leaf.path,
                                  .spelling = leaf.spelling});
    }

    std::optional<Expr> body = lower_declaration(declared, index + 1, next, declaring, depth);
    if (!body)
        return std::nullopt;
    // Innermost member last, so each member's version is established before
    // the body that reads it and the version order matches the binding order.
    for (std::size_t leaf = leaves.size(); leaf > 0; --leaf) {
        body = bind(versions[leaf - 1], place_of(declaring, locals.size() + leaf - 1), std::move(values[leaf - 1]),
                    std::move(*body), declaration, leaves[leaf - 1].type);
    }
    return body;
}

std::optional<Expr> BodyLowering::lower_declaration(const std::vector<CXCursor>& declared, std::size_t index,
                                                    const Continuation& next, const Locals& locals, unsigned depth) {
    if (index == declared.size()) {
        return lower_statements(next, locals, depth + 1);
    }
    const CXCursor declaration = declared[index];
    const std::string name = take(clang_getCursorSpelling(declaration));
    // A static assertion is decided by Clang where it is compiled, and one
    // that fails is a compile error: there is nothing left to model.
    if (clang_getCursorKind(declaration) == CXCursor_StaticAssert) {
        return lower_declaration(declared, index + 1, next, locals, depth);
    }
    if (clang_getCursorKind(declaration) != CXCursor_VarDecl) {
        return reject("only variable declarations are modeled inside a verified body; found '" +
                      take(clang_getCursorKindSpelling(clang_getCursorKind(declaration))) + "'");
    }
    // An invariant the loop lowering did not take is never read as a
    // statement of the body: that would drop it without a word. Nor is a
    // contradiction's block read anywhere but where it opens.
    if (!invariant_prefix.empty() && name.starts_with(invariant_prefix + "contradiction_")) {
        return reject("a claim that a path cannot occur was not read where it was written");
    }
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
    Type type = convert_type(value_type, 0, ReferenceModel::Opaque, refinements);
    // A vector, a string or a span local is the root of a modeled sequence,
    // whose versions carry its length (RFC 0020 §3).
    if (!reference && source::is_sequence(type.representation.kind)) {
        return lower_sequence_declaration(declaration, name, type, declared, index, next, locals, depth);
    }
    // A verified body states a local as one modeled value under logical
    // versioning. A structural value has components rather than such a
    // value, so an aggregate local is tracked as one place per member
    // instead (SPEC.md 12.10): each member is storage of its own, with its
    // own version, and writing one leaves the others alone.
    if (type.kind == TypeKind::Value && !reference) {
        return lower_aggregate(declaration, name, type, declared, index, next, locals, depth);
    }
    if (type.kind == TypeKind::Unsupported || type.kind == TypeKind::Value) {
        return reject("local '" + name + "' has type '" + type.spelling + "', which is not modeled");
    }
    if (refinements != nullptr) {
        auto resolved = refinements_of(declaration, value_type, *refinements);
        if (!resolved)
            return reject(resolved.error().message);
        type.refinements = std::move(*resolved);
    }
    CXCursor initializer = clang_Cursor_getVarDeclInitializer(declaration);
    if (clang_Cursor_isNull(initializer) != 0) {
        return reject("local '" + name + "' is declared without an initializer, so it holds no modeled value");
    }
    std::optional<std::size_t> referent;
    std::optional<Local::Generation> borrows;
    Locals declaring = locals;
    // A reference bound to a temporary extends the temporary's lifetime to
    // its own: it names a new object holding the initializer's value, which
    // nothing else names, so it is that object as a local is (C++
    // [class.temporary]).
    const bool binds_temporary = reference && is_prvalue(initializer);
    if (reference && !binds_temporary && is_sequence_subscript(initializer)) {
        // A reference to a container element is bound to the element place
        // at the current generation, and is usable only while that
        // generation stands (RFC 0020 §4, STDMODEL-015). An element of a
        // span parameter is reached only under a capability an unsafe
        // block can revoke, which a reference could outlive, so it is not
        // bound.
        const bool constant = clang_isConstQualifiedType(clang_getPointeeType(canonical)) != 0;
        const std::size_t before = declaring.size();
        referent = resolve_sequence_element(initializer, declaring,
                                            constant ? Capability::Kind::Readable : Capability::Kind::Writable);
        if (!referent) {
            return std::nullopt;
        }
        for (std::size_t formed = before; formed < declaring.size(); ++formed) {
            formed_derefs.push_back(declaring[formed]);
        }
        if (!declaring[*referent].formed_at.has_value()) {
            return reject("reference '" + name +
                          "' binds an element of a span parameter; a reference is bound only to an element of a "
                          "container this body tracks");
        }
        borrows = declaring[*referent].formed_at;
        if (!same_modeled_value(type, declaring[*referent].type))
            return reject("reference binding changes the modeled value type");
    } else if (reference && !binds_temporary) {
        // A reference denotes existing storage (SPEC.md 12.9), so it binds
        // whatever place its initializer names, through the one access
        // resolver: a local, a member, an element, or a member of one.
        referent = tracked_place(initializer, locals, signature);
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
    declaring.push_back(
        Local{.declaration = declaration, .version = version, .type = type, .referent = referent, .spelling = name});
    declaring.back().borrows = borrows;
    std::optional<Expr> body = lower_declaration(declared, index + 1, next, declaring, depth);
    if (!body) {
        return std::nullopt;
    }
    for (auto changed : invalidated)
        *body = unknown(declaring, changed, std::move(*body), declaration);
    return bind(version, place_of(declaring, declaring.size() - 1), std::move(value), std::move(*body), declaration,
                type);
}

// The place a write targets, resolved the same way a read is.
//
// Every write form - a local, a member, an element, a member of a member -
// resolves through the one access resolver, so a write reaches exactly the
// place written and leaves every place disjoint from it alone (SPEC.md
// 12.10). Only storage this body tracks is ever written.
// The caller storage a reference parameter the callee only reads designates:
// the place this body tracks there, or none for a temporary, which no one
// names after the call. A callee with unsafe code may write that storage
// through the reference (TRUST.md TCB-UNSAFE-004), so an object this body
// reads only as one value, whose post-state no member-by-member effect can
// state, is refused there.
std::optional<std::optional<std::size_t>> BodyLowering::read_reference(CXCursor argument, Locals& locals,
                                                                       CXCursor callee, bool unsafe_callee) {
    if (is_prvalue(argument)) {
        return std::optional<std::size_t>{};
    }
    if (unsafe_callee) {
        if (const auto access = resolve_access(strip_parens(argument)); access && !access->dereferenced) {
            const std::optional<std::size_t> whole = find_local(locals, access->declaration);
            if (whole.has_value() && locals[locals[*whole].referent.value_or(*whole)].read_only) {
                return reject("'" + take(clang_getCursorSpelling(access->declaration)) +
                              "' designates an object this body reads as one value, and it is handed by "
                              "reference to '" +
                              qualified_name_of(callee) +
                              "', whose unsafe code may write it; its post-state is not stated member by member "
                              "(TRUST.md TCB-UNSAFE-004)");
            }
        }
    }
    const std::optional<std::size_t> local = written_local(argument, locals);
    if (!local) {
        return std::nullopt;
    }
    return std::optional<std::optional<std::size_t>>{local};
}

std::optional<std::size_t> BodyLowering::written_local(CXCursor target, Locals& locals) {
    target = strip_parens(target);
    // An element of a vector, a string or a span is written through its
    // element place at the current generation, owing its bound and the
    // element type's refinement (RFC 0020 §3, §6).
    if (is_sequence_subscript(target)) {
        const std::size_t before = locals.size();
        const auto element = resolve_sequence_element(target, locals, Capability::Kind::Writable);
        for (std::size_t index = before; index < locals.size(); ++index) {
            formed_derefs.push_back(locals[index]);
        }
        return element;
    }
    const auto access = resolve_access(target);
    // A write through a pointer is a write to the pointee place, and owes
    // `writable` there. `readable` does not suffice: an output buffer may
    // be writable and not readable, and a readable one may not be written
    // (RFC 0014 §3, SPEC.md VERIFIED-038).
    if (access && access->dereferenced) {
        const std::size_t before = locals.size();
        const auto storage = resolve_storage(target, locals, Capability::Kind::Writable);
        if (!storage) {
            return rejection.empty() ? reject("writing through a pointer requires a memory capability this "
                                              "implementation could not resolve")
                                     : std::nullopt;
        }
        // A pointee written for the first time still needs an entry value:
        // the write establishes the next version, and the version before it
        // must exist for that to be well formed.
        for (std::size_t index = before; index < locals.size(); ++index) {
            if (locals[index].is_deref()) {
                formed_derefs.push_back(locals[index]);
            }
        }
        return storage;
    }
    // A symbolic subscript is written through the same place machinery as
    // any other element: the index owes its bound, and the write reaches
    // every element that may be the one selected.
    if (access && !access->symbolic_indices.empty()) {
        const std::size_t before = locals.size();
        const auto storage = resolve_symbolic_element(locals, *access);
        if (!storage) {
            return rejection.empty() ? reject("this subscript does not name tracked storage") : std::nullopt;
        }
        for (std::size_t index = before; index < locals.size(); ++index) {
            if (locals[index].symbolic) {
                formed_derefs.push_back(locals[index]);
            }
        }
        return storage;
    }
    // A member of the implicit object whose storage may overlap another
    // place, or change unseen, has no place to write (SPEC.md CLASS-010).
    if (clang_getCursorKind(target) == CXCursor_MemberRefExpr && on_implicit_object(target)) {
        if (const std::optional<std::string> unmodeled = unmodeled_member(target)) {
            return reject(*unmodeled + " (SPEC.md CLASS-010, CLASS-015)");
        }
    }
    if (!access) {
        if (clang_getCursorKind(target) == CXCursor_ArraySubscriptExpr) {
            return reject("this subscript does not name one tracked element: writing through a variable index "
                          "requires the extent obligations of RFC 0014, which are not implemented");
        }
        if (clang_getCursorKind(target) == CXCursor_MemberRefExpr) {
            return reject("this member's object is not tracked storage of this body, so writing it has no modeled "
                          "effect");
        }
        return reject("only a local variable is assigned in a modeled body");
    }
    const CXCursor declaration = access->declaration;
    const std::string name = take(clang_getCursorSpelling(declaration));
    const std::optional<std::size_t> local = find_binding(locals, declaration, access->path);
    if (local && locals[locals[*local].referent.value_or(*local)].read_only) {
        return reject("parameter '" + name +
                      "' has no modeled writable storage: the object it designates is "
                      "read here, and its post-state is not stated member by member");
    }
    if (!local) {
        if (!access->path.empty()) {
            return reject("this member's object is not tracked storage of this body, so writing it has no modeled "
                          "effect");
        }
        if (clang_getCursorKind(declaration) == CXCursor_ParmDecl) {
            return reject("parameter '" + name + "' has no modeled writable storage");
        }
        return reject("'" + name + "' is not a local of this body");
    }
    // A reference to a container element is written only while the storage
    // it was bound to is unchanged (STDMODEL-015).
    if (std::optional<std::string> stale = stale_borrow(locals, *local)) {
        return reject(std::move(*stale));
    }
    return local;
}

std::optional<Expr> BodyLowering::write(std::size_t local, Expr value, CXCursor statement, const Continuation& next,
                                        const Locals& locals, unsigned depth) {
    return write_then(local, std::move(value), statement, locals,
                      [&](const Locals& assigned) { return lower_statements(next, assigned, depth + 1); });
}

// Writes `value` to `local`, then lowers what follows the write in the
// state it leaves, with `rest`: what follows a statement, or the next of
// several writes one statement makes.
std::optional<Expr> BodyLowering::write_then(std::size_t local, Expr value, CXCursor statement, const Locals& locals,
                                             const std::function<std::optional<Expr>(const Locals&)>& rest) {
    const std::uint32_t version = next_version++;
    Locals assigned = locals;
    const std::size_t storage = locals[local].referent.value_or(local);
    assigned[storage].version = version;
    const auto invalidated = invalidate_aliases(storage, assigned);
    std::optional<Expr> body = rest(assigned);
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
    valid_versions.insert(version);
    // A place the write may reach holds afterwards either its previous
    // value or the one written, and the write is charged the place's
    // refinement too. Its new version is therefore valid exactly when the
    // previous one was, and is then known to hold a value of its type
    // (SPEC.md REFINE-060, CLASS-010).
    for (const auto index : invalidated) {
        require(locals[index].type);
        const bool valid = valid_versions.contains(locals[index].version);
        if (valid) {
            valid_versions.insert(assigned[index].version);
        }
        *body = unknown(assigned, index, std::move(*body), statement, valid);
    }
    return bind(version, place_of(locals, local), std::move(value), std::move(*body), statement, required);
}

std::optional<Expr> BodyLowering::lower_assignment(CXCursor statement, const Continuation& next, const Locals& locals,
                                                   unsigned depth) {
    const std::vector<CXCursor> operands = children_of(statement);
    if (operands.size() != 2) {
        return reject("an assignment requires a target and a value");
    }
    Locals state = locals;
    const std::optional<std::size_t> local = written_local(operands[0], state);
    if (!local) {
        return std::nullopt;
    }
    const Type type = state[*local].type;
    std::vector<std::size_t> invalidated;
    auto evaluated = evaluate(operands[1], state, invalidated);
    if (!evaluated)
        return std::nullopt;
    Expr value = std::move(*evaluated);
    if (!invalidated.empty())
        return reject("assignment call has uncertain aliases; use a separate call statement");
    // C++ evaluates the assigned value before the place it is assigned to
    // (C++17 [expr.ass]). A call there that may replace a container's storage
    // leaves an element or an element reference resolved on the left
    // designating storage that may be gone (SPEC.md STDMODEL-015).
    if (std::optional<std::string> stale = stale_borrow(state, *local)) {
        return reject(std::move(*stale));
    }
    if (!generation_current(state, state[*local])) {
        return reject("the value assigned to this container element is computed by a call that may replace the "
                      "container's storage; make that call a statement of its own");
    }
    if (!std::holds_alternative<Unsupported>(value.node) && !same_modeled_value(type, value.type)) {
        return reject("assigning '" + value.type.spelling + "' to '" + state[*local].spelling + "' of type '" +
                      type.spelling + "' is a conversion that is not modeled");
    }
    return write(*local, std::move(value), statement, next, state, depth);
}

// `x += e`, `x -= e`, `x *= e`, `x /= e`, `x %= e`, `++x`, `x++`, `--x` and
// `x--` as statements. Each is the assignment `x = x op e` (or `x op 1`) at
// the local's own type, which C++ guarantees exactly when that type is not
// promoted first and `e` is of that type after its own conversions; the
// arithmetic then owes what it owes anywhere (SPEC.md ARITH-013, 12.8).
std::optional<Expr> BodyLowering::lower_update(CXCursor statement, const Continuation& next, const Locals& locals,
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
        } else if (written == CXBinaryOperator_DivAssign) {
            op = BinaryOp::Div;
        } else if (written == CXBinaryOperator_RemAssign) {
            op = BinaryOp::Rem;
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
            return reject(unmodeled_statement("operator '" + take(clang_getUnaryOperatorKindSpelling(written)) + "'"));
        }
        if (operands.size() != 1) {
            return reject("an increment or decrement requires one operand");
        }
    }

    Locals state = locals;
    const bool element = is_sequence_subscript(operands[0]);
    // A compound update reads the place and then writes it, so it owes both
    // capabilities. Neither entails the other, so both are required
    // explicitly (RFC 0014 §3). A container element read here is formed at
    // the current generation and bound before the statement, like any
    // element the statement reads (RFC 0020 §3).
    if (element) {
        const std::size_t before = state.size();
        if (!resolve_sequence_element(operands[0], state, Capability::Kind::Readable)) {
            return std::nullopt;
        }
        for (std::size_t formed = before; formed < state.size(); ++formed) {
            formed_derefs.push_back(state[formed]);
        }
    } else if (const auto access = resolve_access(strip_parens(operands[0])); access && access->dereferenced) {
        if (!resolve_storage(strip_parens(operands[0]), state, Capability::Kind::Readable)) {
            return rejection.empty() ? reject("updating through a pointer requires a readable capability")
                                     : std::nullopt;
        }
    }
    const std::optional<std::size_t> local = written_local(operands[0], state);
    if (!local) {
        return std::nullopt;
    }
    const Local target = state[*local];
    const std::string name = target.spelling;
    // The promotion question is about the storage being updated, which for a
    // member is the member's own type, not its object's, and for a container
    // element is the element's.
    if (promoted_before_arithmetic(element ? clang_getCursorType(strip_parens(operands[0]))
                                           : clang_getCursorType(target.path.empty()
                                                                     ? target.declaration
                                                                     : clang_getCursorReferenced(operands[0])))) {
        return reject("updating '" + name + "' of type '" + target.type.spelling +
                      "' is computed after promotion to a wider type and converted back, which is not modeled");
    }

    // The update reads the place it writes, through the one read path: a
    // compound assignment is `x = x op e` at the same storage.
    Expr current = read_place(state, target.referent.value_or(*local), operands[0]);

    Expr amount;
    if (operands.size() == 2) {
        if (!materialize(operands[1], state)) {
            return std::nullopt;
        }
        amount = build_expression(operands[1], signature, state, 0);
        if (!std::holds_alternative<Unsupported>(amount.node) && !same_modeled_value(target.type, amount.type)) {
            return reject("updating '" + name + "' of type '" + target.type.spelling + "' by '" + amount.type.spelling +
                          "' is a conversion that is not modeled");
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
    return write(*local, std::move(value), statement, next, state, depth);
}

// The template arguments a specialization was instantiated at, as Clang
// resolved them (SPEC.md 42).
//
// Clang owns substitution: these are read back only to pair a specialization
// with the instantiation of its own contract, never to perform substitution
// here. A form this implementation does not read becomes `Other`, which
// compares equal only to the same position of another argument list and so
// never merges two specializations that differ in it.
std::vector<TemplateArgument> template_arguments_of(CXCursor cursor) {
    std::vector<TemplateArgument> arguments;
    const int count = clang_Cursor_getNumTemplateArguments(cursor);
    for (int index = 0; index < count; ++index) {
        const auto position = static_cast<unsigned>(index);
        TemplateArgument argument;
        switch (clang_Cursor_getTemplateArgumentKind(cursor, position)) {
            case CXTemplateArgumentKind_Integral:
                argument.kind = TemplateArgument::Kind::Integral;
                argument.integral = clang_Cursor_getTemplateArgumentValue(cursor, position);
                break;
            case CXTemplateArgumentKind_Type: {
                argument.kind = TemplateArgument::Kind::Type;
                const CXType type = clang_Cursor_getTemplateArgumentType(cursor, position);
                argument.spelling = take(clang_getTypeSpelling(clang_getCanonicalType(type)));
                break;
            }
            default:
                argument.kind = TemplateArgument::Kind::Other;
                argument.spelling = std::to_string(index);
                break;
        }
        arguments.push_back(std::move(argument));
    }
    return arguments;
}

// Whether `cursor` is a specialization of a function template, and if so the
// primary template it came from.
std::optional<CXCursor> specialized_template(CXCursor cursor) {
    if (clang_Cursor_getNumTemplateArguments(cursor) <= 0) {
        return std::nullopt;
    }
    const CXCursor primary = clang_getSpecializedCursorTemplate(cursor);
    if (clang_Cursor_isNull(primary) != 0) {
        return std::nullopt;
    }
    return primary;
}

struct RefinedTemplateArgument {
    std::string refinement;
    std::string template_name;
};

std::optional<RefinedTemplateArgument> refined_template_argument(CXCursor cursor, const Selection& selection,
                                                                 unsigned depth = 0);

std::string refined_template_argument_refusal(const RefinedTemplateArgument& refined) {
    return "refinement '" + refined.refinement + "' is written as a template argument of '" + refined.template_name +
           "', whose instantiation holds it as its base type, where nothing charges its predicate; a refinement is a "
           "template argument only where the sequence model states its elements (SPEC.md STDMODEL-020)";
}

struct Collector {
    const Selection* selection = nullptr;
    std::vector<CXCursor> selected;
    // Uses of a verified template's specialization at a refined argument.
    std::vector<std::pair<std::string, CXCursor>> refused_arguments;
    // Specializations of verified function templates this unit instantiated,
    // deduplicated by USR.
    std::vector<CXCursor> specializations;
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

    // A member function is selected like any other: a verified member by the
    // offset of its declaration in the class, and a contract probe of one --
    // itself a member of that class -- by its generated name.
    if ((kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod) && is_selected(cursor, *collector.selection)) {
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
    // A variable declared outside a verified body is storage ordinary C++
    // establishes without any obligation, so a refinement on it would be a fact
    // nothing proved.
    //
    // A data member is different: it has no value of its own until an object is
    // constructed, and every construction and write is checked where it happens
    // (SPEC.md 17.6). Declaring one is therefore sound, and the objects built
    // from it are what carry the obligations.
    if (kind == CXCursor_VarDecl)
        collector.unverified_storage.push_back(cursor);

    return collector.selection->refinements.empty() ? CXChildVisit_Continue : CXChildVisit_Recurse;
}

// Collect the specializations of function templates that this unit actually
// instantiated (SPEC.md 42, TEMPLATE-001).
//
// An implicit instantiation is not a child of the translation unit cursor, so
// it cannot be found by walking declarations: it is reached from the use that
// caused it. Every reference is followed and the referenced declaration taken,
// which is the specialization Clang selected and instantiated. Nothing here
// decides which specialization a use denotes -- Clang already did, including
// overload resolution and constraints (SPEC.md TEMPLATE-002).
CXChildVisitResult collect_specializations(CXCursor cursor, CXCursor, CXClientData data) {
    auto& collector = *static_cast<Collector*>(data);
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind != CXCursor_CallExpr && kind != CXCursor_DeclRefExpr && kind != CXCursor_MemberRefExpr) {
        return CXChildVisit_Recurse;
    }
    const CXCursor referenced = clang_getCursorReferenced(cursor);
    if (clang_Cursor_isNull(referenced) != 0) {
        return CXChildVisit_Recurse;
    }
    const auto primary = specialized_template(referenced);
    if (!primary) {
        return CXChildVisit_Recurse;
    }
    // Only a specialization of a declaration this unit marked verified is
    // checked here, or of a probe the projector generated for one. The
    // primary's own location is what the projector recorded, because that is
    // where the author wrote the declaration.
    const auto offset = physical_offset(*primary);
    const auto name = take(clang_getCursorSpelling(referenced));
    const bool generated = !collector.selection->specification_prefix.empty() &&
                           name.starts_with(collector.selection->specification_prefix);
    if (!generated && std::ranges::find(collector.selection->offsets, offset) == collector.selection->offsets.end()) {
        return CXChildVisit_Recurse;
    }
    if (const auto refined = refined_template_argument(cursor, *collector.selection)) {
        collector.refused_arguments.emplace_back(refined_template_argument_refusal(*refined), cursor);
    }
    const auto usr = take(clang_getCursorUSR(referenced));
    const bool known = std::ranges::any_of(
        collector.specializations, [&](CXCursor candidate) { return take(clang_getCursorUSR(candidate)) == usr; });
    if (!known) {
        collector.specializations.push_back(referenced);
        // The instantiated body refers to this specialization's own contract
        // probes, which C++ instantiates at the same arguments. Those
        // instantiations exist only inside the body, so they are collected by
        // descending into it: that is what makes the proposition checked the
        // one written for these arguments (SPEC.md TEMPLATE-001).
        clang_visitChildren(referenced, collect_specializations, &collector);
    }
    return CXChildVisit_Recurse;
}

// Detect an explicit refinement use at an unverified storage/callable boundary.
// These are Clang declaration-reference edges, including ordinary aliases and
// type constructors; no pointer/pointee or container-wide fact is inferred.
std::optional<std::string> refinement_use(CXCursor declaration, const Selection& selection, unsigned depth = 0,
                                          std::unordered_set<std::size_t>* visited = nullptr) {
    if (depth > kMaxExpressionDepth)
        return "unresolved alias chain";

    // Record types reach one another, and themselves: a glibc `FILE` is a
    // `struct _IO_FILE` whose fields point back at `_IO_FILE`. Walking that
    // without remembering where we have been revisits the same declarations
    // until the depth guard trips, and the guard's "unresolved alias chain"
    // would then be reported as a refinement on a standard header that
    // declares none. A declaration is therefore visited once per query.
    std::unordered_set<std::size_t> owned;

    if (visited == nullptr)
        visited = &owned;

    if (!visited->insert(physical_offset(declaration)).second)
        return std::nullopt;
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
                if (auto use = refinement_use(referenced, selection, depth + 1, visited))
                    return use;
            }
        }
        if (kind == CXCursor_TypeAliasDecl) {
            if (auto use = refinement_use(child, selection, depth + 1, visited))
                return use;
        }
        // A record's refined member is storage this declaration establishes
        // too. Constructing the object outside a verified body would put a
        // value in that member without proving its predicate, so the record
        // counts as a refinement use exactly as a directly refined type does
        // (SPEC.md 17.6).
        if (kind == CXCursor_TypeRef) {
            const auto definition = clang_getCursorDefinition(clang_getCursorReferenced(child));
            const auto definition_kind = clang_getCursorKind(definition);
            if (definition_kind == CXCursor_StructDecl || definition_kind == CXCursor_ClassDecl) {
                for (const auto field : children_of(definition)) {
                    if (clang_getCursorKind(field) != CXCursor_FieldDecl)
                        continue;
                    if (auto use = refinement_use(field, selection, depth + 1, visited))
                        return use;
                }
            }
        }
    }
    return std::nullopt;
}

bool in_namespace_std(CXCursor cursor) {
    for (CXCursor scope = clang_getCursorSemanticParent(cursor);
         clang_Cursor_isNull(scope) == 0 && clang_getCursorKind(scope) != CXCursor_TranslationUnit;
         scope = clang_getCursorSemanticParent(scope)) {
        if (clang_getCursorKind(scope) == CXCursor_Namespace && take(clang_getCursorSpelling(scope)) == "std") {
            return true;
        }
    }
    return false;
}

// A refinement written as a template argument of a template outside the
// standard library, among the references `cursor` holds directly: a
// declaration whose type names such a template, or a reference to a
// specialization of such a function template. The instantiation holds the
// refinement as its base type, so nothing there charges the predicate while
// the refinement is still written (SPEC.md STDMODEL-020). A standard template
// with a refined argument is governed by the sequence model or refused by it.
std::optional<RefinedTemplateArgument> refined_template_argument(CXCursor cursor, const Selection& selection,
                                                                 unsigned depth) {
    if (selection.refinements.empty() || depth > kMaxExpressionDepth) {
        return std::nullopt;
    }
    std::optional<std::string> user_template;
    // A refinement a template's own parameter takes by default reaches every
    // use that leaves that argument out.
    const auto defaulted = [&](CXCursor named_template) -> std::optional<RefinedTemplateArgument> {
        for (const CXCursor parameter : children_of(named_template)) {
            if (clang_getCursorKind(parameter) != CXCursor_TemplateTypeParameter) {
                continue;
            }
            for (const CXCursor argument : children_of(parameter)) {
                if (clang_getCursorKind(argument) != CXCursor_TypeRef) {
                    continue;
                }
                if (auto refinement = refinement_use(clang_getCursorReferenced(argument), selection)) {
                    return RefinedTemplateArgument{std::move(*refinement), qualified_name_of(named_template)};
                }
            }
        }
        return std::nullopt;
    };
    const CXCursor referenced = clang_getCursorReferenced(cursor);
    if (clang_isExpression(clang_getCursorKind(cursor)) != 0 && clang_Cursor_isNull(referenced) == 0) {
        const CXCursor primary = clang_getSpecializedCursorTemplate(referenced);
        if (clang_Cursor_isNull(primary) == 0 && !in_namespace_std(primary)) {
            user_template = qualified_name_of(primary);
            if (auto by_default = defaulted(primary)) {
                return by_default;
            }
        }
    }
    const CXCursor initializer = clang_Cursor_getVarDeclInitializer(cursor);
    for (const CXCursor child : children_of(cursor)) {
        if (clang_Cursor_isNull(initializer) == 0 && clang_equalCursors(child, initializer) != 0) {
            break;
        }
        const CXCursorKind kind = clang_getCursorKind(child);
        if (kind == CXCursor_TemplateRef) {
            const CXCursor named = clang_getCursorReferenced(child);
            if (!user_template.has_value() && !in_namespace_std(named)) {
                user_template = qualified_name_of(named);
                if (auto by_default = defaulted(named)) {
                    return by_default;
                }
            }
            continue;
        }
        // `Box<decltype(p)>` names `p`'s type through `p`.
        if (kind == CXCursor_DeclRefExpr && user_template.has_value()) {
            if (auto refinement = refinement_use(clang_getCursorReferenced(child), selection)) {
                return RefinedTemplateArgument{std::move(*refinement), *user_template};
            }
            continue;
        }
        if (kind != CXCursor_TypeRef) {
            continue;
        }
        const CXCursor named = clang_getCursorReferenced(child);
        if (user_template.has_value()) {
            if (auto refinement = refinement_use(named, selection)) {
                return RefinedTemplateArgument{std::move(*refinement), *user_template};
            }
        }
        // An alias of such a specialization hides it from the declaration.
        const CXCursorKind named_kind = clang_getCursorKind(named);
        if (named_kind == CXCursor_TypeAliasDecl || named_kind == CXCursor_TypedefDecl) {
            if (auto hidden = refined_template_argument(named, selection, depth + 1)) {
                return hidden;
            }
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

// Whether a conjunction joins a memory capability with an ordinary predicate
// somewhere among its conjuncts.
bool mixes_capabilities(const source::ProjectionShape& shape, unsigned depth = 0) {
    if (depth > kMaxExpressionDepth || shape.kind != source::ProjectionKind::Conjunction) {
        return false;
    }
    return std::ranges::any_of(shape.children, [&](const source::ProjectionShape& child) {
        return is_capability_shape(child) || mixes_capabilities(child, depth + 1);
    });
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

} // namespace detail

// The bridge's own functions that places.hpp declares for the lowering of
// whole struct values, so it reads types, places and accesses as the body
// lowering does.
namespace detail::bridge {

Type convert_type(CXType type, unsigned depth, ReferenceModel references,
                  const std::vector<Selection::Refinement>* known) {
    return detail::convert_type(type, depth, references, known);
}
bool same_modeled_value(const Type& outer, const Type& inner) {
    return detail::same_modeled_value(outer, inner);
}
bool same_term(const Expr& lhs, const Expr& rhs) {
    return detail::same_term(lhs, rhs);
}
std::string qualified_name_of(CXCursor cursor) {
    return detail::qualified_name_of(cursor);
}
Expr unsupported_expression(CXCursor cursor, std::string reason) {
    return detail::unsupported_expression(cursor, std::move(reason));
}
source::ParameterPassing passing_of(CXType written) {
    return detail::passing_of(written);
}
CXCursor designated_object(CXCursor expression) {
    return detail::designated_object(expression);
}
std::optional<ResolvedAccess> resolve_access(CXCursor cursor) {
    return detail::resolve_access(cursor);
}
std::optional<std::size_t> find_binding(const Locals& locals, CXCursor declaration, const std::vector<PlaceStep>& path,
                                        const Expr* index_term) {
    return detail::find_binding(locals, declaration, path, index_term);
}
std::optional<std::string> stale_borrow(const Locals& locals, std::size_t binding) {
    return detail::stale_borrow(locals, binding);
}
Place place_of(const Locals& locals, std::size_t entry) {
    return detail::place_of(locals, entry);
}
Expr read_place(const Locals& locals, std::size_t entry, CXCursor at) {
    return detail::read_place(locals, entry, at);
}
Place anonymous_place(std::string spelling) {
    return BodyLowering::anonymous_place(std::move(spelling));
}

} // namespace detail::bridge

// What the bridge's entry points use of its internals.
namespace {
using detail::collect;
using detail::collect_specializations;
using detail::Collector;
using detail::convert_severity;
using detail::convert_type;
using detail::extract_body;
using detail::extract_formal;
using detail::member_standing;
using detail::MemberStanding;
using detail::mixes_capabilities;
using detail::parameters_of;
using detail::passing_of;
using detail::physical_offset;
using detail::qualified_name_of;
using detail::ReceiverLeaf;
using detail::reference_value_type;
using detail::ReferenceModel;
using detail::refined_template_argument;
using detail::refined_template_argument_refusal;
using detail::refinement_use;
using detail::refinements_of;
using detail::Signature;
using detail::specialized_template;
using detail::StatedCapability;
using detail::template_arguments_of;
using detail::UnsafeEffects;
using detail::bridge::presumed_location;
using detail::bridge::take;
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

// The one function declared at `offset`, or nothing when two different
// functions share it: an ambiguous declaration is not resolved by guessing.
//
// Two records of the *same* function are not two functions. An explicit
// specialization is reached both as a declaration and as its definition, and
// both carry one USR, so identity decides this rather than record count.
const Function* TranslationUnit::find_at_offset(std::size_t offset) const {
    const Function* found = nullptr;
    for (const Function& function : functions) {
        if (function.analysis_offset == offset) {
            if (found != nullptr && found->usr != function.usr)
                return nullptr;
            // Prefer the record carrying a body: the contract is discharged
            // from the definition.
            if (found == nullptr || (function.has_body && !found->has_body))
                found = &function;
        }
    }
    return found;
}

std::vector<const Function*> TranslationUnit::find_specializations_at_offset(std::size_t offset) const {
    std::vector<const Function*> found;
    for (const Function& function : functions) {
        if (function.analysis_offset == offset && !function.primary_usr.empty()) {
            found.push_back(&function);
        }
    }
    return found;
}

// Every specialization of a verified function template the unit references, by
// USR with one cursor for it, as Clang's indexer reports references from every
// body it instantiated, implicit instantiations included: a destructor's, a
// conversion's, or a template's reached only through another template. The
// cursor search in `collect_specializations` sees only what an expression of
// the written program names, and no instantiated body (SPEC.md TEMPLATE-001).
// Nothing when the indexer fails, which the caller refuses.
std::optional<std::vector<std::pair<std::string, CXCursor>>> indexed_specializations(CXIndex index,
                                                                                     CXTranslationUnit unit,
                                                                                     const Selection& selection) {
    struct Found {
        const Selection* selection = nullptr;
        std::vector<std::pair<std::string, CXCursor>> specializations;
    } found{&selection, {}};
    IndexerCallbacks callbacks{};
    callbacks.indexEntityReference = [](CXClientData data, const CXIdxEntityRefInfo* reference) {
        if (reference == nullptr || reference->referencedEntity == nullptr) {
            return;
        }
        auto& into = *static_cast<Found*>(data);
        // The indexer names a specialization by its template; the reference
        // itself, in the instantiated body, resolves to the specialization.
        const CXCursor referenced = clang_getCursorReferenced(reference->cursor);
        if (clang_Cursor_isNull(referenced) != 0) {
            return;
        }
        const auto primary = specialized_template(referenced);
        if (!primary.has_value()) {
            return;
        }
        const std::string name = take(clang_getCursorSpelling(referenced));
        const bool generated =
            !into.selection->specification_prefix.empty() && name.starts_with(into.selection->specification_prefix);
        if (!generated &&
            std::ranges::find(into.selection->offsets, physical_offset(*primary)) == into.selection->offsets.end()) {
            return;
        }
        std::string usr = take(clang_getCursorUSR(referenced));
        if (std::ranges::none_of(into.specializations, [&](const auto& known) { return known.first == usr; })) {
            into.specializations.emplace_back(std::move(usr), referenced);
        }
    };
    CXIndexAction action = clang_IndexAction_create(index);
    if (action == nullptr) {
        return std::nullopt;
    }
    const int status = clang_indexTranslationUnit(action, &found, &callbacks, sizeof(callbacks),
                                                  CXIndexOpt_IndexImplicitTemplateInstantiations, unit);
    clang_IndexAction_dispose(action);
    if (status != 0) {
        return std::nullopt;
    }
    return std::move(found.specializations);
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

    // What every type below was resolved for. The caller compares it with the
    // target the program is compiled for (TRUST.md TCB-CLANG-006).
    if (CXTargetInfo target = clang_getTranslationUnitTargetInfo(unit); target != nullptr) {
        result.target = take(clang_TargetInfo_getTriple(target));
        clang_TargetInfo_dispose(target);
    }

    const unsigned diagnostic_count = clang_getNumDiagnostics(unit);
    for (unsigned index_of_diagnostic = 0; index_of_diagnostic < diagnostic_count; ++index_of_diagnostic) {
        CXDiagnostic diagnostic = clang_getDiagnostic(unit, index_of_diagnostic);
        Diagnostic converted;
        converted.severity = convert_severity(clang_getDiagnosticSeverity(diagnostic));
        converted.category = Category::CppSemantic;
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
    // A second pass for template specializations, which are reached from their
    // uses rather than from the declaration list (SPEC.md 42).
    clang_visitChildren(clang_getTranslationUnitCursor(unit), collect_specializations, &collector);
    // Every specialization of a verified function template the unit
    // instantiates is verified: one the search above did not reach, from an
    // implicit call it cannot see, is refused rather than left unverified
    // (SPEC.md TEMPLATE-001).
    const bool verified_templates = std::ranges::any_of(collector.functions, [&](CXCursor function) {
        return clang_getCursorKind(function) == CXCursor_FunctionTemplate &&
               std::ranges::find(request.selection.offsets, physical_offset(function)) !=
                   request.selection.offsets.end();
    });
    if (verified_templates) {
        const auto indexed = indexed_specializations(index, unit, request.selection);
        if (!indexed.has_value()) {
            // libclang's indexer failed on a unit Clang accepted: the bridge
            // could not run, which says nothing about the program.
            result.has_errors = true;
            result.diagnostics.push_back(
                {Severity::Error, Category::Internal,
                 "the instantiations of this unit's verified function templates could not be enumerated, so which "
                 "of them are verified cannot be shown (SPEC.md TEMPLATE-001)",
                 presumed_location(clang_getCursorLocation(clang_getTranslationUnitCursor(unit)))});
        } else {
            // A specialization the cursor search did not reach, such as one a
            // class template's destructor uses, is verified as every other is,
            // with whatever its own body instantiates.
            for (const auto& [usr, specialization] : *indexed) {
                if (std::ranges::any_of(collector.specializations, [&, usr = std::string_view(usr)](CXCursor known) {
                        return take(clang_getCursorUSR(known)) == usr;
                    })) {
                    continue;
                }
                collector.specializations.push_back(specialization);
                clang_visitChildren(specialization, collect_specializations, &collector);
            }
        }
    }
    // In a verified declaration or body, every declaration and reference that
    // names a template outside the standard library at a refined argument
    // (SPEC.md STDMODEL-020).
    for (const CXCursor& function : collector.selected) {
        if (const auto refined = refined_template_argument(function, request.selection)) {
            collector.refused_arguments.emplace_back(refined_template_argument_refusal(*refined), function);
        }
        clang_visitChildren(
            function,
            [](CXCursor cursor, CXCursor, CXClientData data) {
                auto& found = *static_cast<Collector*>(data);
                const CXCursorKind kind = clang_getCursorKind(cursor);
                if (kind == CXCursor_VarDecl || kind == CXCursor_ParmDecl || kind == CXCursor_DeclRefExpr ||
                    kind == CXCursor_TypeAliasDecl || kind == CXCursor_TypedefDecl) {
                    if (const auto refined = refined_template_argument(cursor, *found.selection)) {
                        found.refused_arguments.emplace_back(refined_template_argument_refusal(*refined), cursor);
                    }
                }
                return CXChildVisit_Recurse;
            },
            &collector);
    }
    std::vector<std::pair<std::string, source::SourceLocation>> reported;
    for (const auto& [message, at] : collector.refused_arguments) {
        std::pair<std::string, source::SourceLocation> refusal{message, presumed_location(clang_getCursorLocation(at))};
        if (std::ranges::find(reported, refusal) != reported.end()) {
            continue;
        }
        reported.push_back(refusal);
        result.has_errors = true;
        // Well-formed C++ whose instantiation this implementation does not
        // model with the predicate (SPEC.md STDMODEL-020).
        result.diagnostics.push_back({Severity::Error, Category::UnsupportedSemantics, refusal.first, refusal.second});
    }
    // Nothing proof-only text names may be instantiated in the program verified
    // where the program run does not instantiate it (SPEC.md ERASE-019). A unit
    // Clang already refused is not asked: its recovery AST is never verified.
    if (!result.has_errors) {
        for (Diagnostic& refusal :
             detail::proof_only_instantiations(unit, request.selection, collector.specializations)) {
            result.has_errors = true;
            result.diagnostics.push_back(std::move(refusal));
        }
    }
    for (const CXCursor& specialization : collector.specializations) {
        collector.selected.push_back(specialization);
    }
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
        // A template's specializations are the functions that get verified, and
        // each reports the primary's location rather than its own. The
        // declaration the author marked `verified` is therefore the primary, so
        // a specialization is matched through it (SPEC.md TEMPLATE-001).
        const auto declared_offset = [](CXCursor candidate) {
            const auto primary = specialized_template(candidate);
            return physical_offset(primary.value_or(candidate));
        };
        const bool verified =
            std::ranges::find(request.selection.verified_offsets, declared_offset(cursor)) !=
                request.selection.verified_offsets.end() ||
            std::ranges::any_of(collector.selected, [&](CXCursor candidate) {
                return clang_equalCursors(canonical, clang_getCanonicalCursor(candidate)) &&
                       std::ranges::find(request.selection.verified_offsets, declared_offset(candidate)) !=
                           request.selection.verified_offsets.end();
            });
        // Ordinary C++ that Clang accepted, refused because no trusted
        // refinement boundary is modeled: not a C++ error.
        if (!verified) {
            result.has_errors = true;
            result.diagnostics.push_back(
                {Severity::Error, Category::UnsupportedSemantics,
                 "ordinary function '" + qualified_name_of(cursor) + "' return cannot establish refinement '" +
                     *refined + "'; verify its definition (explicit trusted refinement boundaries are not implemented)",
                 presumed_location(clang_getCursorLocation(cursor))});
        }
    }
    for (const auto declaration : collector.unverified_storage) {
        if (request.selection.refinements.empty())
            break;
        // Ordinary C++ that Clang accepted, refused because an unverified
        // construction boundary is not modeled: not a C++ error.
        if (const auto refined = refinement_use(declaration, request.selection)) {
            result.has_errors = true;
            result.diagnostics.push_back(
                {Severity::Error, Category::UnsupportedSemantics,
                 "storage '" + take(clang_getCursorSpelling(declaration)) + "' uses refinement '" + *refined +
                     "' outside a modeled verified body, where ordinary C++ could establish it without proof; a "
                     "verified body checks its own construction and writes, but an unverified construction boundary "
                     "is not yet checked",
                 presumed_location(clang_getCursorLocation(declaration))});
        }
    }

    // The memory capabilities each verified function's contract states, keyed by
    // the analysis offset of the declaration they belong to.
    //
    // They are collected before any body is lowered because a probe is an
    // ordinary function of this unit and may be parsed after the body it
    // constrains. A body may rely only on what its own contract states
    // (SPEC.md VERIFIED-043).
    // Keyed by the analysis offset of the verified function the clause belongs
    // to, so a body may rely only on its own contract.
    std::unordered_map<std::size_t, std::vector<StatedCapability>> stated_capabilities;
    UnsafeEffects unsafe_effects(request.selection.specification_prefix, request.selection.unsafe_symbols);
    for (const auto& probe : request.selection.proposition_probes) {
        if (probe.shape.kind != source::ProjectionKind::Readable &&
            probe.shape.kind != source::ProjectionKind::Writable &&
            probe.shape.kind != source::ProjectionKind::Capabilities && !mixes_capabilities(probe.shape)) {
            continue;
        }
        const auto at = std::ranges::find_if(collector.functions, [&](CXCursor candidate) {
            return take(clang_getCursorSpelling(candidate)) == probe.name;
        });
        if (at == collector.functions.end()) {
            continue;
        }
        Function resolved;
        // A member function's probe is a member too, so a pointer it names
        // stands past the implicit object's leaves (SPEC.md CLASS-008).
        MemberStanding standing = member_standing(*at, request.selection.refinements);
        extract_formal(resolved, *at,
                       Signature{parameters_of(*at), std::move(standing.receiver), true, request.selection.refinements},
                       probe.shape);
        if (resolved.capabilities.empty()) {
            continue;
        }
        // The probe's parameters mirror the verified function's, so the index
        // each capability resolved against is the function's own parameter.
        const auto owner = std::ranges::find_if(request.selection.clause_owners,
                                                [&](const auto& candidate) { return candidate.probe == probe.owner; });
        if (owner == request.selection.clause_owners.end()) {
            continue;
        }
        for (const Capability& capability : resolved.capabilities) {
            StatedCapability stated;
            stated.parameter = capability.pointer.root.id;
            stated.kind = capability.kind;
            stated.extent = capability.extent;
            stated_capabilities[owner->function_offset].push_back(stated);
        }
    }

    for (const CXCursor& cursor : collector.selected) {
        Function function;
        function.usr = take(clang_getCursorUSR(cursor));
        function.external_linkage = clang_getCursorLinkage(cursor) == CXLinkage_External;
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
                                       projected_expression ? ReferenceModel::Referent : ReferenceModel::Opaque,
                                       &request.selection.refinements);
        function.location = presumed_location(clang_getCursorLocation(cursor));
        function.analysis_offset = physical_offset(cursor);
        // A specialization records the template it came from and the arguments
        // it was instantiated at. Its `usr` already differs per specialization,
        // so this identifies which declaration's contract it carries without
        // ever merging two of them (SPEC.md TEMPLATE-001, TEMPLATE-003).
        if (const auto primary = specialized_template(cursor); primary.has_value()) {
            function.primary_usr = take(clang_getCursorUSR(*primary));
            function.template_arguments = template_arguments_of(cursor);
            // An implicit instantiation carries the contract written on the
            // primary, so it is keyed to the primary's declaration: the
            // contract written once is found for every specialization of it.
            //
            // An explicit specialization states its own contract at its own
            // location, and the projector recorded that declaration rather
            // than the primary's. Re-keying it to the primary would hand it a
            // contract written for a different body and would collide with the
            // primary's own declaration (SPEC.md TEMPLATE-001, TEMPLATE-003).
            //
            // An implicit instantiation reports the primary's own location,
            // while an explicit specialization is written somewhere else and
            // reports that. Comparing the two is what separates them: the C
            // API exposes no specialization-kind predicate.
            const bool states_own_contract =
                physical_offset(cursor) != physical_offset(*primary) &&
                std::ranges::find(request.selection.verified_offsets, physical_offset(cursor)) !=
                    request.selection.verified_offsets.end();
            if (!states_own_contract) {
                function.analysis_offset = physical_offset(*primary);
                function.location = presumed_location(clang_getCursorLocation(*primary));
            }
        }

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
                result.diagnostics.push_back({Severity::Error, resolved.error().category, resolved.error().message,
                                              presumed_location(clang_getCursorLocation(declaration))});
            }
        };
        attach_refinements(function.result, cursor, clang_getCursorResultType(cursor));

        // A member function's implicit object: one reference parameter per
        // leaf, standing before the written ones (SPEC.md CLASS-008). One this
        // implementation does not verify is refused by name, and its body is
        // not lowered as though it were verified without its object.
        const bool executable = std::ranges::find(request.selection.verified_offsets, function.analysis_offset) !=
                                request.selection.verified_offsets.end();
        MemberStanding standing = member_standing(cursor, request.selection.refinements);
        if (standing.rejection.has_value()) {
            if (executable) {
                function.member_rejection = std::move(standing.rejection);
            } else {
                function.has_body = true;
                function.body_rejection = std::move(standing.rejection);
            }
            result.functions.push_back(std::move(function));
            continue;
        }
        if (standing.receiver.has_value()) {
            for (const ReceiverLeaf& leaf : standing.receiver->leaves) {
                function.parameters.push_back(Parameter{leaf.spelling, leaf.type, standing.receiver->passing(leaf)});
            }
        }

        const std::vector<CXCursor> parameter_cursors = parameters_of(cursor);
        for (const CXCursor& parameter : parameter_cursors) {
            // A proof binder is projected as a reference parameter so Clang
            // resolves it without requiring a copy, a move, a default
            // constructor or any runtime object. It denotes the subject's own
            // value, so the referent is what it means.
            const auto written = clang_getCursorType(parameter);
            const auto passing = passing_of(written);
            Type parameter_type = convert_type(written, 0, ReferenceModel::Referent, &request.selection.refinements);
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
            // A templated probe is reached through the reference that forced its
            // instantiation, which names a declaration; the proposition it
            // states lives in the definition. Clang owns which declaration is
            // the definition, so it is asked rather than assumed.
            const CXCursor defined = clang_getCursorDefinition(cursor);
            const CXCursor stating = clang_Cursor_isNull(defined) != 0 ? cursor : defined;
            extract_formal(
                function, stating,
                Signature{parameters_of(stating), std::move(standing.receiver), true, request.selection.refinements},
                probe->shape);
        } else {
            // Clang owns declaration/definition identity, including overloads
            // and parameter renaming. The public declaration supplies contract
            // metadata; the resolved definition supplies the executable body.
            const CXCursor definition = clang_getCursorDefinition(cursor);
            const CXCursor body_cursor = clang_Cursor_isNull(definition) ? cursor : definition;
            const auto stated = stated_capabilities.find(function.analysis_offset);
            // A body a verified function states is lowered as a body; anything
            // else selected here is a clause or a definition the formal core
            // may use, which reads a member of the implicit object as the
            // parameter it is.
            extract_body(function, body_cursor,
                         Signature{parameters_of(body_cursor), std::move(standing.receiver), !executable,
                                   request.selection.refinements, executable ? &unsafe_effects : nullptr},
                         request.selection.specification_prefix.empty() ? std::string()
                                                                        : request.selection.specification_prefix,
                         request.selection.refinements, executable,
                         stated == stated_capabilities.end() ? nullptr : &stated->second, unsafe_effects);
        }
        result.functions.push_back(std::move(function));
    }

    return result;
}

std::string clang_version() {
    return take(clang_getClangVersion());
}

} // namespace cppl::clangbridge
