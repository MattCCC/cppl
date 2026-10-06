#include "access.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "expressions.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "refinements.hpp"
#include "sequences.hpp"
#include "signature.hpp"
#include "types.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// A modeled sequence's statements in a verified body (RFC 0020 §3, §4, §6): a
// mutator written as a statement, a library summary whose effect is a new
// storage generation, and a vector, string or span local's construction, whose
// one modeled fact is its length.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::strip_parens;

// A mutator of a modeled sequence written as a statement (RFC 0020 §6): a
// call to a trusted library summary whose effect is a new version of the
// container's root, which is a new storage generation (§4). From here on,
// no element place formed before is matched and every view or element
// reference formed before is stale (STDMODEL-015). A value it puts into an
// element owes the element type's refinement before the call.
std::optional<Expr> BodyLowering::lower_sequence_statement(const SequenceCall& call, CXCursor statement,
                                                           const Continuation& next, const Locals& locals,
                                                           unsigned depth) {
    using K = source::RepresentationKind;
    using Op = source::LibraryOperation;
    const std::string qualified = library_name(call.family, call.name);
    if (!source::is_sequence(call.family) || call.constructor) {
        return reject("'" + qualified + "' is not a modeled statement (SPEC.md STDMODEL-019)");
    }
    Locals state = locals;
    const std::optional<std::size_t> root = owning_root(call.object, state);
    if (!root) {
        return reject("'" + qualified +
                      "' is modeled only on a vector or string this body names directly (SPEC.md STDMODEL-013)");
    }
    library_models.insert(call.family);
    const std::string name = state[*root].spelling;
    Type element_type;
    {
        // Read before anything is added to `state`, which may move entries.
        const std::optional<Local::Sequence>& rooted = state[*root].sequence;
        if (!rooted.has_value()) {
            return reject("'" + qualified + "' is modeled only on a vector or string this body tracks");
        }
        element_type = rooted->element;
    }
    const std::vector<CXCursor> formals = parameters_of(call.method);
    const auto formal = [&](std::size_t position) {
        return position < formals.size() ? clang_getCanonicalType(clang_getCursorType(formals[position]))
                                         : CXType{CXType_Invalid, {nullptr, nullptr}};
    };
    // Whether a formal parameter is a reference to the container's own
    // class: the overload taking another container of the same type.
    const CXCursor own_class =
        clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(strip_parens(call.object))));
    const auto of_own_class = [&](CXType reference, CXTypeKind kind) {
        return reference.kind == kind &&
               clang_equalCursors(clang_getTypeDeclaration(clang_getCanonicalType(clang_getPointeeType(reference))),
                                  own_class) != 0;
    };
    std::optional<Op> operation;
    std::optional<CXCursor> pushed_argument;
    std::optional<CXCursor> count_argument;
    std::optional<std::size_t> source;
    // `s += c` appends one character, as `push_back` does.
    const bool appends_character = call.family == K::String && call.name == "operator+=" &&
                                   call.arguments.size() == 1 &&
                                   (formal(0).kind == CXType_Char_S || formal(0).kind == CXType_Char_U);
    if ((call.name == "push_back" && call.arguments.size() == 1) || appends_character) {
        operation = Op::PushBack;
        pushed_argument = call.arguments.front();
    } else if (call.family == K::String && (call.name == "operator+=" || call.name == "append") &&
               call.arguments.size() == 1 && of_own_class(formal(0), CXType_LValueReference)) {
        operation = Op::Append;
        source = owning_root(call.arguments.front(), state);
    } else if (call.name == "pop_back" && call.arguments.empty()) {
        operation = Op::PopBack;
    } else if (call.name == "clear" && call.arguments.empty()) {
        operation = Op::Clear;
    } else if (call.name == "reserve" && call.arguments.size() == 1) {
        operation = Op::Reserve;
        count_argument = call.arguments.front();
    } else if (call.name == "operator=" && call.arguments.size() == 1 &&
               (of_own_class(formal(0), CXType_LValueReference) || of_own_class(formal(0), CXType_RValueReference))) {
        const bool moving = formal(0).kind == CXType_RValueReference;
        operation = moving ? Op::MoveAssign : Op::Assign;
        const std::optional<CXCursor> operand = moving ? moved_operand(call.arguments.front()) : call.arguments.front();
        source = operand ? owning_root(*operand, state) : std::optional<std::size_t>{};
    }
    if (!operation) {
        return reject("'" + qualified + "' is not a modeled operation of " +
                      std::string(source::describe_model(call.family)) + " (SPEC.md STDMODEL-019)");
    }
    if ((*operation == Op::Append || *operation == Op::Assign || *operation == Op::MoveAssign) && !source) {
        return reject("'" + qualified + "' is modeled only with a container this body tracks as its argument");
    }
    if (source.has_value() && (*operation == Op::Assign || *operation == Op::MoveAssign)) {
        if (*source == *root) {
            return reject("assigning '" + name + "' to itself is not modeled");
        }
        if (auto gap = refinement_gap(state[*root], state[*source])) {
            return reject(std::move(*gap));
        }
        if (*operation == Op::MoveAssign && state[*source].external) {
            return reject("'" + name + "' is assigned by moving from '" + state[*source].spelling +
                          "', which is caller storage; only a container this body owns is moved from");
        }
    }

    std::optional<Expr> pushed;
    if (pushed_argument) {
        if (!materialize(*pushed_argument, state)) {
            return std::nullopt;
        }
        pushed = build_expression(*pushed_argument, signature, state, 0);
        if (!std::holds_alternative<Unsupported>(pushed->node) && !same_modeled_value(element_type, pushed->type)) {
            return reject("pushing '" + pushed->type.spelling + "' into '" + name + "' of element type '" +
                          element_type.spelling + "' is a conversion that is not modeled");
        }
    }
    std::vector<Expr> arguments{read_root(state, *root, statement), read_root(state, *root, statement)};
    if (count_argument) {
        if (!materialize(*count_argument, state)) {
            return std::nullopt;
        }
        Expr count = build_expression(*count_argument, signature, state, 0);
        if (!std::holds_alternative<Unsupported>(count.node) &&
            !same_modeled_value(state[*root].type.projections.front(), count.type)) {
            return reject("reserving '" + count.type.spelling + "' elements of '" + name +
                          "' is a conversion to its size type that is not modeled");
        }
        arguments.push_back(std::move(count));
    }
    if (source) {
        arguments.push_back(read_root(state, *source, statement));
        if (*operation == Op::MoveAssign) {
            arguments.push_back(read_root(state, *source, statement));
        }
    }

    // Versions in evaluation order: the pushed value, then the effects.
    const std::optional<std::uint32_t> pushed_version =
        pushed ? std::optional<std::uint32_t>{next_version++} : std::nullopt;
    const std::string where = "'" + qualified + "' at " + describe_location(statement);
    std::vector<CallEffect> effects;
    effects.push_back(new_generation(state[*root], where));
    std::vector<std::size_t> invalidated = invalidate_aliases(*root, state);
    // A move assignment has its source: one without was refused above.
    if (*operation == Op::MoveAssign && source.has_value()) {
        CallEffect moved = new_generation(state[*source], "being moved from by " + where);
        moved.argument = 2;
        effects.push_back(std::move(moved));
        for (const std::size_t changed : invalidate_aliases(*source, state)) {
            if (std::ranges::find(invalidated, changed) == invalidated.end()) {
                invalidated.push_back(changed);
            }
        }
    }
    Type nothing;
    nothing.kind = TypeKind::Void;
    nothing.spelling = "void";
    Expr value =
        library_call({call.family, *operation}, state[*root].type, qualified, std::move(arguments), nothing, statement);
    std::get<Call>(value.node).effects = std::move(effects);
    const std::uint32_t discarded = next_version++;

    std::optional<Expr> body = lower_statements(next, state, depth + 1);
    if (!body) {
        return std::nullopt;
    }
    for (const std::size_t changed : std::views::reverse(invalidated)) {
        *body = unknown(state, changed, std::move(*body), statement);
    }
    body = bind(discarded, anonymous_place("library call"), std::move(value), std::move(*body), statement);
    if (pushed) {
        body = bind(*pushed_version, anonymous_place("element pushed into " + name), std::move(*pushed),
                    std::move(*body), statement, element_type);
    }
    return body;
}

std::optional<CXCursor> BodyLowering::moved_operand(CXCursor cursor) {
    return moved_operand_of(cursor);
}

// Why the elements of `source` are not known to satisfy what `target`'s
// element type requires, if they are not: a copy or move carries the
// values, never a proof they meet a refinement the source never owed.
std::optional<std::string> BodyLowering::refinement_gap(const Local& target, const Local& source) {
    if (!target.sequence.has_value() || !source.sequence.has_value()) {
        return "'" + source.spelling + "' and '" + target.spelling + "' are not both containers this body tracks";
    }
    const std::vector<Refinement>& held = source.sequence->element.refinements;
    for (const Refinement& refinement : target.sequence->element.refinements) {
        if (std::ranges::find(held, refinement) == held.end()) {
            return "the elements of '" + source.spelling + "' are not known to satisfy '" + refinement.name +
                   "', which the elements of '" + target.spelling + "' require";
        }
    }
    return std::nullopt;
}

Expr BodyLowering::read_root(const Locals& state, std::size_t root, CXCursor at) const {
    return read_place(state, root, at);
}

// A vector, a string or a span local (RFC 0020 §3, §6): the root of a
// modeled sequence, established by one of the modeled constructors as a
// trusted library summary whose one fact is the length.
//
// A value the construction puts into an element is a refinement crossing
// into the element type, owed where it enters (SPEC.md 17.2): a listed
// element, a fill value, or the value-initialized element of a sized one. A
// listed element is also bound to its place, so `v[0]` after `{1, 2}` is 1.
std::optional<Expr> BodyLowering::lower_sequence_declaration(CXCursor declaration, const std::string& name,
                                                             const Type& type, const std::vector<CXCursor>& declared,
                                                             std::size_t index, const Continuation& next,
                                                             const Locals& locals, unsigned depth) {
    using K = source::RepresentationKind;
    const K family = type.representation.kind;
    if (!type.representation.rejection.empty() || type.projections.size() != 1) {
        return reject("local '" + name + "' has type '" + type.spelling +
                      "', which is not modeled: " + type.representation.rejection);
    }
    library_models.insert(family);
    const CXCursor written_initializer = clang_Cursor_getVarDeclInitializer(declaration);
    if (clang_Cursor_isNull(written_initializer) != 0) {
        return reject("local '" + name + "' is declared without an initializer, so it holds no modeled value");
    }
    const std::optional<SequenceCall> constructed = sequence_call(strip_parens(written_initializer));
    if (!constructed || !constructed->constructor) {
        return reject("local '" + name + "' of type '" + type.spelling +
                      "' is not initialized by a modeled constructor (SPEC.md STDMODEL-013)");
    }
    const Type& length = type.projections.front();
    const auto count_literal = [&](std::int64_t value) {
        Expr literal;
        literal.type = length;
        literal.location = presumed_location(clang_getCursorLocation(written_initializer));
        literal.node = IntLiteral{value};
        return literal;
    };
    const std::vector<CXCursor> formals = parameters_of(constructed->method);
    const auto formal = [&](std::size_t position) {
        return position < formals.size() ? clang_getCanonicalType(clang_getCursorType(formals[position]))
                                         : CXType{CXType_Invalid, {nullptr, nullptr}};
    };
    const auto of_this_class = [&](CXType reference) {
        return clang_equalCursors(clang_getTypeDeclaration(clang_getCanonicalType(clang_getPointeeType(reference))),
                                  clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(declaration)))) !=
               0;
    };

    Locals declaring = locals;
    Local root{.declaration = declaration, .type = type, .spelling = name};
    // Clang caches a nested aggregate as not default-constructible while its
    // enclosing class is incomplete, so emplace() would not compile.
    Local::Sequence& held = root.sequence.emplace(Local::Sequence{});
    held.kind = family;
    source::LibraryOperation operation = source::LibraryOperation::Construct;
    std::vector<Expr> arguments;
    std::vector<CallEffect> effects;
    std::vector<std::pair<Expr, Type>> charged;
    std::vector<Expr> listed;
    std::vector<std::size_t> invalidated;
    const std::size_t count = constructed->arguments.size();

    if (family == K::Span) {
        // A span local views a whole vector or string this body tracks, and
        // is usable while that storage's generation stands (STDMODEL-015).
        const std::optional<std::size_t> viewed =
            count == 1 ? owning_root(constructed->arguments.front(), declaring) : std::nullopt;
        const std::optional<Local::Sequence>* viewed_sequence =
            viewed.has_value() ? &declaring[*viewed].sequence : nullptr;
        if (!viewed || viewed_sequence == nullptr || !viewed_sequence->has_value()) {
            return reject("span '" + name +
                          "' is modeled only as a view of a whole vector or string this body tracks "
                          "(SPEC.md STDMODEL-014)");
        }
        // Its elements are the viewed storage's, under that storage's
        // content invariant if it has one; a refinement written as its own
        // element type would state nothing (SPEC.md STDMODEL-020).
        if (auto written = sequence_element(declaration, clang_getCursorType(declaration), refinements);
            !written || !written->refinements.empty()) {
            return reject("span '" + name + "' " +
                          (written ? "is declared with the refined element type '" + written->refinements.front().name +
                                         "'; a span's elements are the storage it views, and a refined element "
                                         "type states a content invariant only of a vector local (SPEC.md "
                                         "STDMODEL-020)"
                                   : written.error()));
        }
        operation = source::LibraryOperation::ViewOf;
        held.element = (*viewed_sequence)->element;
        held.external_elements = (*viewed_sequence)->external_elements;
        held.views = viewed;
        root.borrows = Local::Generation{*viewed, declaring[*viewed].version};
        arguments.push_back(read_root(declaring, *viewed, written_initializer));
    } else {
        auto element = sequence_element(declaration, clang_getCursorType(declaration), refinements);
        if (!element) {
            return reject("local '" + name + "': " + element.error());
        }
        held.element = std::move(*element);
        const Type& element_type = held.element;
        const CXType first = formal(0);
        if (count == 0) {
            arguments.push_back(count_literal(0));
        } else if (count == 1 && is_standard_template(first, "initializer_list")) {
            CXCursor list = constructed->arguments.front();
            for (unsigned step = 0; step < kMaxExpressionDepth && clang_getCursorKind(list) != CXCursor_InitListExpr;
                 ++step) {
                const std::vector<CXCursor> inner = children_of(list);
                if (inner.size() != 1) {
                    break;
                }
                list = inner.front();
            }
            if (clang_getCursorKind(list) != CXCursor_InitListExpr) {
                return reject("the initializer list of '" + name + "' was not resolved");
            }
            for (const CXCursor item : children_of(list)) {
                auto value = evaluate(item, declaring, invalidated);
                if (!value) {
                    return std::nullopt;
                }
                if (!invalidated.empty()) {
                    return reject("an element of '" + name +
                                  "' is computed by a call with effects; call it in a "
                                  "statement of its own");
                }
                if (!std::holds_alternative<Unsupported>(value->node) &&
                    !same_modeled_value(element_type, value->type)) {
                    return reject("an element of '" + name + "' of type '" + element_type.spelling + "' is '" +
                                  value->type.spelling + "', a conversion that is not modeled");
                }
                listed.push_back(std::move(*value));
            }
            if (listed.size() > kMaxTrackedLeaves) {
                return reject("'" + name + "' lists more elements than the proof resource limit allows");
            }
            arguments.push_back(count_literal(static_cast<std::int64_t>(listed.size())));
        } else if ((count == 1 || count == 2) && convert_type(first).kind == TypeKind::Int &&
                   (count != 2 || family != K::String)) {
            // `S v(n)` holds `n` value-initialized elements; `S v(n, x)` holds
            // `n` copies of `x`. Either value enters the element type.
            if (!materialize(constructed->arguments[0], declaring)) {
                return std::nullopt;
            }
            Expr size = build_expression(constructed->arguments[0], signature, declaring, 0);
            if (!std::holds_alternative<Unsupported>(size.node) && !same_modeled_value(length, size.type)) {
                return reject("the length of '" + name + "' is '" + size.type.spelling +
                              "', a conversion to its size type that is not modeled");
            }
            arguments.push_back(std::move(size));
            if (count == 2) {
                if (!materialize(constructed->arguments[1], declaring)) {
                    return std::nullopt;
                }
                Expr fill = build_expression(constructed->arguments[1], signature, declaring, 0);
                if (!std::holds_alternative<Unsupported>(fill.node) && !same_modeled_value(element_type, fill.type)) {
                    return reject("the fill value of '" + name + "' is a conversion that is not modeled");
                }
                charged.emplace_back(std::move(fill), element_type);
            } else if (element_type.kind == TypeKind::Int || element_type.kind == TypeKind::Bool) {
                Expr zero;
                zero.type = element_type;
                zero.location = presumed_location(clang_getCursorLocation(written_initializer));
                zero.node = IntLiteral{0};
                charged.emplace_back(std::move(zero), element_type);
            }
        } else if (family == K::String && count == 1 &&
                   clang_getCursorKind(strip_parens(constructed->arguments.front())) == CXCursor_StringLiteral) {
            // `basic_string(const char*)` takes the characters up to the
            // first null, which is exactly what Clang's evaluation of the
            // literal as a C string yields.
            // Clang evaluates the pointer the literal decays to as the
            // literal it points at.
            CXEvalResult evaluated = clang_Cursor_Evaluate(constructed->arguments.front());
            if (evaluated == nullptr) {
                return reject("the string literal initializing '" + name + "' could not be evaluated");
            }
            const bool text = clang_EvalResult_getKind(evaluated) == CXEval_StrLiteral;
            const std::size_t characters = text ? std::string_view(clang_EvalResult_getAsStr(evaluated)).size() : 0;
            clang_EvalResult_dispose(evaluated);
            if (!text) {
                return reject("the string literal initializing '" + name + "' could not be evaluated");
            }
            arguments.push_back(count_literal(static_cast<std::int64_t>(characters)));
        } else if (count == 1 && (first.kind == CXType_LValueReference || first.kind == CXType_RValueReference) &&
                   of_this_class(first)) {
            const bool moving = first.kind == CXType_RValueReference;
            const std::optional<CXCursor> operand =
                moving ? moved_operand(constructed->arguments.front()) : constructed->arguments.front();
            const std::optional<std::size_t> origin =
                operand ? owning_root(*operand, declaring) : std::optional<std::size_t>{};
            if (!origin) {
                return reject("'" + name + "' is " + (moving ? "moved" : "copied") +
                              " from something other than a container this body tracks");
            }
            if (auto gap = refinement_gap(root, declaring[*origin])) {
                return reject(std::move(*gap));
            }
            arguments.push_back(read_root(declaring, *origin, written_initializer));
            if (moving) {
                // A move takes the source's storage: every view of it and
                // every fact about it end here (SPEC.md STORAGE-008,
                // STDMODEL-015, STDMODEL-021). A source that is caller storage could be
                // what a capability designates, so it is not moved from.
                if (declaring[*origin].external) {
                    return reject("'" + name + "' is moved from '" + declaring[*origin].spelling +
                                  "', which is caller storage; only a container this body owns is moved from");
                }
                operation = source::LibraryOperation::Move;
                arguments.push_back(read_root(declaring, *origin, written_initializer));
                effects.push_back(new_generation(declaring[*origin],
                                                 "being moved from at " + describe_location(written_initializer)));
                invalidated = invalidate_aliases(*origin, declaring);
            } else {
                operation = source::LibraryOperation::Copy;
            }
        } else {
            return reject("this constructor of '" + type.spelling + "' is not modeled (SPEC.md STDMODEL-013)");
        }
    }

    // Versions are numbered in evaluation order: what enters an element
    // first, then the constructed value, then the listed elements.
    std::vector<std::uint32_t> charged_versions;
    charged_versions.reserve(charged.size());
    for (std::size_t position = 0; position < charged.size(); ++position) {
        charged_versions.push_back(next_version++);
    }
    // The root: the constructed value, stated by the summary.
    Expr constructed_value =
        library_call({family, operation}, type, library_name(family, std::string(source::describe(operation))),
                     std::move(arguments), type, written_initializer);
    std::get<Call>(constructed_value.node).effects = std::move(effects);
    root.version = next_version++;
    const std::size_t root_index = declaring.size();
    declaring.push_back(root);

    // Each listed element is the place `v[j]` names at this generation.
    std::vector<std::uint32_t> element_versions;
    for (std::size_t position = 0; position < listed.size(); ++position) {
        Local element;
        element.declaration = declaration;
        element.version = next_version++;
        element.type = held.element;
        element.path = {PlaceStep{PlaceStep::Kind::Element, static_cast<std::uint32_t>(position), 0}};
        element.spelling = name + "[" + std::to_string(position) + "]";
        element.symbolic = true;
        element.index_value.push_back(count_literal(static_cast<std::int64_t>(position)));
        Expr extent;
        extent.type = length;
        extent.location = presumed_location(clang_getCursorLocation(declaration));
        extent.node = Projection{0, {read_root(declaring, root_index, declaration)}};
        element.extent.push_back(std::move(extent));
        element.formed_at = Local::Generation{root_index, root.version};
        element_versions.push_back(element.version);
        declaring.push_back(std::move(element));
    }

    std::optional<Expr> body = lower_declaration(declared, index + 1, next, declaring, depth);
    if (!body) {
        return std::nullopt;
    }
    for (std::size_t position = listed.size(); position > 0; --position) {
        body = bind(element_versions[position - 1], place_of(declaring, root_index + position),
                    std::move(listed[position - 1]), std::move(*body), declaration, held.element);
    }
    for (const std::size_t changed : std::views::reverse(invalidated)) {
        *body = unknown(declaring, changed, std::move(*body), declaration);
    }
    body = bind(root.version, place_of(declaring, root_index), std::move(constructed_value), std::move(*body),
                declaration, type);
    // A value entering an element owes the element type's refinement where
    // it enters, before the construction that stores it.
    for (std::size_t position = charged.size(); position > 0; --position) {
        body =
            bind(charged_versions[position - 1], anonymous_place("element of " + name),
                 std::move(charged[position - 1].first), std::move(*body), declaration, charged[position - 1].second);
    }
    return body;
}

} // namespace cppl::clangbridge::detail
