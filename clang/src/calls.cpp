#include "access.hpp"
#include "aggregate_values.hpp"
#include "call_objects.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
#include "default_arguments.hpp"
#include "expressions.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "sequences.hpp"
#include "signature.hpp"
#include "statements.hpp"
#include "types.hpp"
#include "unsafe.hpp"

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

// A call in a verified body (SPEC.md CLASS-011, VERIFIED-030 to VERIFIED-041,
// RFC 0020 §7): a full expression evaluated once, the storage it hands its
// callee by reference, by pointer, as a view or as a data pointer, and the
// post-call versions it leaves to that storage and to whatever may alias it.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::strip_parens;
using bridge::take;

std::optional<BodyLowering::HandedStorage> BodyLowering::handed_storage(CXCursor argument, const Locals& state) const {
    CXCursor stripped = strip_parens(argument);
    if (clang_getCursorKind(stripped) == CXCursor_CXXFunctionalCastExpr) {
        for (const CXCursor child : children_of(stripped)) {
            if (clang_isExpression(clang_getCursorKind(child)) != 0) {
                stripped = strip_parens(child);
                break;
            }
        }
    }
    if (const std::optional<SequenceCall> call = sequence_call(stripped)) {
        if (call->constructor && call->family == source::RepresentationKind::Span && call->arguments.size() == 1) {
            if (const std::optional<std::size_t> root = owning_root(call->arguments.front(), state)) {
                return HandedStorage{root, std::nullopt, source::RepresentationKind::Span};
            }
            // A span passed by value is a copy of a span local or
            // parameter, and hands on the storage that one designates.
            return handed_storage(call->arguments.front(), state);
        }
        if (!call->constructor && call->name == "data" && source::is_sequence(call->family)) {
            stripped = strip_parens(call->object);
        } else {
            return std::nullopt;
        }
    }
    if (clang_getCursorKind(stripped) != CXCursor_DeclRefExpr) {
        return std::nullopt;
    }
    const CXCursor declaration = clang_getCursorReferenced(stripped);
    if (const auto binding = find_binding(state, declaration)) {
        const std::size_t storage = state[*binding].referent.value_or(*binding);
        const std::optional<Local::Sequence>& held = state[storage].sequence;
        if (!held.has_value()) {
            return std::nullopt;
        }
        return HandedStorage{held->views.value_or(storage), std::nullopt, held->kind};
    }
    const Type type = convert_type(clang_getCursorType(declaration));
    if (type.representation.kind == source::RepresentationKind::Span &&
        std::ranges::any_of(parameters,
                            [&](CXCursor candidate) { return clang_equalCursors(candidate, declaration); })) {
        return HandedStorage{std::nullopt, declaration, source::RepresentationKind::Span};
    }
    return std::nullopt;
}

// What a verified call owes for the container storage it hands its callee
// as a span or a data pointer (RFC 0020 §7, SPEC.md STDMODEL-016,
// STDMODEL-017).
//
// The storage a capability designates must not be element storage of a
// container the callee may reallocate: a callee may keep reading a span
// while it appends to a vector it holds by reference only because the two
// are proved apart here. A callee that may write through what it is handed
// leaves the elements unknown afterwards, and could store an unrefined value
// in a refined container, so that is refused.
std::optional<std::string> BodyLowering::view_arguments(CXCursor call, const std::vector<CXCursor>& formals,
                                                        Locals& state, std::vector<std::size_t>& invalidated,
                                                        std::vector<std::size_t>& written_roots, bool unsafe_callee) {
    // The containers the callee receives by mutable reference, or by any
    // reference when its unsafe code may write through one, and so may
    // reallocate (TRUST.md TCB-UNSAFE-004).
    std::vector<std::size_t> reallocatable;
    for (std::size_t index = 0; index < formals.size(); ++index) {
        const source::ParameterPassing passing = passing_of(clang_getCursorType(formals[index]));
        if (!source::may_write(passing) && !(unsafe_callee && source::aliases_storage(passing))) {
            continue;
        }
        if (const auto root = owning_root(clang_Cursor_getArgument(call, static_cast<unsigned>(index)), state)) {
            reallocatable.push_back(*root);
        }
    }
    std::vector<CXCursor> written_span_parameters;
    for (std::size_t index = 0; index < formals.size(); ++index) {
        const CXCursor argument = clang_Cursor_getArgument(call, static_cast<unsigned>(index));
        const CXType written = clang_getCanonicalType(clang_getCursorType(formals[index]));
        const Type formal = convert_type(written);
        const bool span = formal.representation.kind == source::RepresentationKind::Span;
        const bool pointer = written.kind == CXType_Pointer;
        if (!span && !pointer) {
            continue;
        }
        const std::optional<HandedStorage> handed = handed_storage(argument, state);
        if (!handed) {
            continue;
        }
        library_models.insert(handed->family);
        // Whether the callee may write the elements through it: a span of
        // non-const elements, or a pointer to non-const.
        const CXType element = span ? clang_Type_getTemplateArgumentAsType(written, 0) : clang_getPointeeType(written);
        const bool writes = clang_isConstQualifiedType(element) == 0;
        if (handed->root.has_value()) {
            const std::size_t root = *handed->root;
            for (const std::size_t other : reallocatable) {
                if (other == root || may_alias(state[other], state[root])) {
                    return "'" + state[root].spelling + "' is handed to '" +
                           qualified_name_of(clang_getCursorReferenced(call)) +
                           "' as a view or data pointer and, in the same call, by a reference through which the "
                           "callee may reallocate it; the storage a capability designates must not be storage "
                           "the callee can replace (SPEC.md STDMODEL-016)";
                }
            }
            if (!writes && unsafe_callee) {
                // A view of `const` elements whose callee's unsafe code may
                // write through it: every element place of the container is
                // unknown after the call, as one a written view reaches, and
                // a content invariant could not be kept.
                if (const auto& held = *state[root].sequence; !held.element.refinements.empty()) {
                    return "the elements of '" + state[root].spelling + "' are handed to '" +
                           qualified_name_of(clang_getCursorReferenced(call)) +
                           "', whose unsafe code may write them, and nothing obliges it to write values "
                           "satisfying '" +
                           state[root].sequence->element.refinements.front().name + "' (TRUST.md TCB-UNSAFE-004)";
                }
                for (std::size_t other = 0; other < state.size(); ++other) {
                    if (state[other].referent.has_value() || !state[other].formed_at.has_value() ||
                        state[other].formed_at->root != root) {
                        continue;
                    }
                    state[other].version = next_version++;
                    invalidated.push_back(other);
                    for (const std::size_t aliased : invalidate_aliases(other, state)) {
                        invalidated.push_back(aliased);
                    }
                }
                continue;
            }
            if (!writes) {
                continue;
            }
            if (!state[root].sequence->element.refinements.empty()) {
                return "the elements of '" + state[root].spelling +
                       "' are handed to a callee that may write them, and nothing obliges it to write values "
                       "satisfying '" +
                       state[root].sequence->element.refinements.front().name + "'";
            }
            for (const std::size_t other : written_roots) {
                if (root == other || may_alias(state[root], state[other])) {
                    return "'" + state[root].spelling + "' is handed to '" +
                           qualified_name_of(clang_getCursorReferenced(call)) +
                           "' through two views or data pointers the callee may write; one storage written "
                           "through two arguments of one call has no single post-state (SPEC.md STDMODEL-017)";
                }
            }
            written_roots.push_back(root);
            // Every element place of the container is unknown after the
            // call; the container's length is not, since no element write
            // changes it.
            for (std::size_t other = 0; other < state.size(); ++other) {
                if (state[other].referent.has_value() || !state[other].formed_at.has_value() ||
                    state[other].formed_at->root != root) {
                    continue;
                }
                state[other].version = next_version++;
                invalidated.push_back(other);
                for (const std::size_t aliased : invalidate_aliases(other, state)) {
                    invalidated.push_back(aliased);
                }
            }
        } else if (handed->span_parameter.has_value() && (writes || unsafe_callee)) {
            if (writes && std::ranges::any_of(written_span_parameters, [&](CXCursor written_parameter) {
                    return clang_equalCursors(written_parameter, *handed->span_parameter) != 0;
                })) {
                return "span parameter '" + take(clang_getCursorSpelling(*handed->span_parameter)) +
                       "' is handed to '" + qualified_name_of(clang_getCursorReferenced(call)) +
                       "' twice as a view the callee may write; one storage written through two arguments of "
                       "one call has no single post-state (SPEC.md STDMODEL-017)";
            }
            if (writes) {
                written_span_parameters.push_back(*handed->span_parameter);
            }
            for (std::size_t other = 0; other < state.size(); ++other) {
                if (state[other].referent.has_value() ||
                    clang_equalCursors(state[other].declaration, *handed->span_parameter) == 0 ||
                    state[other].path.empty()) {
                    continue;
                }
                state[other].version = next_version++;
                invalidated.push_back(other);
                for (const std::size_t aliased : invalidate_aliases(other, state)) {
                    invalidated.push_back(aliased);
                }
            }
        }
    }
    return std::nullopt;
}

// Evaluate a full expression once, then advance the storage touched by its
// call. The continuation sees only these post-call versions.
std::optional<Expr> BodyLowering::evaluate(CXCursor cursor, Locals& state, std::vector<std::size_t>& invalidated) {
    while (clang_getCursorKind(cursor) == CXCursor_UnexposedExpr || clang_getCursorKind(cursor) == CXCursor_ParenExpr) {
        const auto children = children_of(cursor);
        if (children.size() != 1 || !same_modeled_value(convert_type(clang_getCursorType(cursor)),
                                                        convert_type(clang_getCursorType(children.front()))))
            break;
        cursor = children.front();
    }
    // A copy or a move C++ defines memberwise is the value it copies, so
    // what is evaluated is that value: the call whose effects follow, when
    // it is one (TRUST.md TCB-AGGREGATE-001).
    cursor = aggregates::copied_value(cursor);
    if (const ChosenArm* chosen = chosen_for(chosen_arms, cursor)) {
        if (!chosen->arm)
            return chosen->constant;
        cursor = *chosen->arm;
    }
    if (!form_places(cursor, state)) {
        return std::nullopt;
    }
    Expr value = runtime_value(build_expression(cursor, signature, state, 0, true), signature.clause);
    auto* call = std::get_if<Call>(&value.node);
    if (!call)
        return value;
    const auto callee = clang_getCursorReferenced(cursor);
    const auto params = parameters_of(callee);
    // A callee whose unsafe code may write what it is handed through a
    // `const` access path writes, as far as this body can tell, every
    // reference, pointer and view it is handed, and its contract describes
    // each at the value it leaves there (TRUST.md TCB-UNSAFE-004).
    const bool unsafe_callee = !call->library.has_value() && unsafe_effects != nullptr && unsafe_effects->of(callee);
    // The containers whose elements the callee may write through a view or
    // a data pointer it is handed.
    std::vector<std::size_t> written_roots;
    if (!call->library.has_value()) {
        if (std::optional<std::string> refused =
                view_arguments(cursor, params, state, invalidated, written_roots, unsafe_callee)) {
            return reject(std::move(*refused));
        }
    }
    // A callee that takes a pointer to non-const may write through it, and
    // the caller's facts about the pointee do not survive that. This is
    // separate from the reference case below: a pointer is passed by value,
    // so the parameter keeps its own version and it is the storage it
    // designates that goes stale (SPEC.md 12.10 VERIFIED-040).
    //
    // Which storage that is does not depend on which places this body has
    // formed through the pointer: every place that may alias an arbitrary
    // pointee -- this pointer's or another's pointee, storage a reference
    // parameter designates, an escaped local, a container reached through
    // one -- is unknown after the call, exactly as after a write through
    // `*p` in this body (VERIFIED-039). Places this call hands the callee by
    // reference take its effects instead, so they are left to that.
    const bool writes_through_pointer = std::ranges::any_of(params, [&](CXCursor parameter) {
        const CXType declared = clang_getCursorType(parameter);
        return !source::aliases_storage(passing_of(declared)) &&
               (may_write_through(declared) || (unsafe_callee && designates_storage(declared)));
    });
    const auto havoc_pointees = [&](const std::vector<std::size_t>& handed) {
        if (!writes_through_pointer) {
            return;
        }
        for (const std::size_t reached : invalidate_pointee_aliases(state, handed, invalidated)) {
            invalidated.push_back(reached);
        }
    };
    // A member function called on an object takes the object's leaves as
    // the arguments of its implicit object (SPEC.md CLASS-011). The build
    // above resolved both already, or the value would not be a call.
    std::optional<Receiver> callee_receiver;
    std::optional<CallObject> object;
    if (clang_getCursorKind(callee) == CXCursor_CXXMethod && clang_CXXMethod_isStatic(callee) == 0) {
        auto receiver = receiver_of(callee, nullptr);
        auto resolved = call_object(cursor, callee);
        if (!receiver || !resolved) {
            return reject("the object of a member call was not resolved");
        }
        callee_receiver = std::move(*receiver);
        object = std::move(*resolved);
    }
    const std::uint32_t offset =
        callee_receiver.has_value() ? static_cast<std::uint32_t>(callee_receiver->leaves.size()) : 0;
    const bool writes = (callee_receiver.has_value() && callee_receiver->writes()) ||
                        std::ranges::any_of(params, [](CXCursor parameter) {
                            return source::may_write(passing_of(clang_getCursorType(parameter)));
                        });
    if (!writes && !unsafe_callee) {
        havoc_pointees({});
        return value;
    }
    // The caller's storage at each position the callee reads or writes by
    // reference: its implicit object's places first, then its reference
    // parameters (SPEC.md CLASS-011). A position is writable where the
    // callee binds it writable; one bound `const` it only reads.
    struct Position {
        std::uint32_t argument = 0;
        std::size_t storage = 0;
        bool writable = false;
    };
    std::vector<Position> positions;
    // The struct arguments this body tracks member by member, each a group
    // of places one reference parameter designates (TRUST.md
    // TCB-AGGREGATE-001).
    std::vector<aggregates::ArgumentGroup> groups;
    // The receiver and the object are resolved together, or neither is.
    if (callee_receiver.has_value() && object.has_value()) {
        for (std::size_t leaf = 0; leaf < callee_receiver->leaves.size(); ++leaf) {
            std::vector<PlaceStep> path = object->path;
            path.insert(path.end(), callee_receiver->leaves[leaf].path.begin(),
                        callee_receiver->leaves[leaf].path.end());
            const auto target = object_place(state, parameters, *object, path);
            if (!target) {
                // An object a parameter designates by reference is read as
                // one value, never written member by member.
                const std::optional<std::size_t> whole = find_local(state, object->declaration);
                if (whole.has_value() && state[*whole].read_only) {
                    return reject("'" + qualified_name_of(callee) + "' may write the object it is called on, " +
                                  "which a parameter designates by reference; such an object is read here as " +
                                  "one value and is not written member by member");
                }
                return reject("the object of a member call has storage this body does not track where '" +
                              callee_receiver->leaves[leaf].spelling + "' stands");
            }
            positions.push_back(Position{static_cast<std::uint32_t>(leaf), *target,
                                         source::may_write(callee_receiver->passing(callee_receiver->leaves[leaf]))});
        }
    }
    for (std::size_t index = 0; index < params.size(); ++index) {
        const source::ParameterPassing passing = passing_of(clang_getCursorType(params[index]));
        if (!source::aliases_storage(passing))
            continue;
        // A reference parameter's default binds storage the call does not
        // name -- a global, or a temporary -- so there is no place of this
        // body to hand the callee, as for a written argument that is not
        // one (SPEC.md R.16).
        if (is_default_argument(clang_Cursor_getArgument(cursor, static_cast<unsigned>(index)))) {
            return reject("the default argument of reference " + default_owner(callee, static_cast<unsigned>(index)) +
                          " binds storage this call does not name, which is not modeled (SPEC.md R.16)");
        }
        const CXCursor argument = clang_Cursor_getArgument(cursor, static_cast<unsigned>(index));
        if (std::optional<aggregates::ArgumentGroup> group = aggregates::argument_group(
                argument, clang_getPointeeType(clang_getCanonicalType(clang_getCursorType(params[index]))), state,
                frame_of(signature))) {
            group->argument = offset + static_cast<std::uint32_t>(index);
            group->writable = source::may_write(passing);
            groups.push_back(std::move(*group));
            continue;
        }
        std::optional<std::size_t> target;
        if (source::may_write(passing)) {
            target = written_local(argument, state);
            if (!target)
                return std::nullopt;
        } else {
            // A reference the callee only reads: the caller storage it
            // designates, or none for a temporary, which no one names after
            // the call.
            const std::optional<std::optional<std::size_t>> read =
                read_reference(argument, state, callee, unsafe_callee);
            if (!read)
                return std::nullopt;
            if (!read->has_value()) {
                if (unsafe_callee) {
                    // The callee's contract describes the temporary at the
                    // value its unsafe code leaves there, which nothing
                    // states.
                    const CXType referee =
                        clang_getPointeeType(clang_getCanonicalType(clang_getCursorType(params[index])));
                    Type declared = convert_type(clang_getCanonicalType(clang_getUnqualifiedType(referee)));
                    call->effects.push_back(
                        CallEffect{offset + static_cast<std::uint32_t>(index), next_version++, std::move(declared)});
                }
                continue;
            }
            target = *read;
        }
        const auto storage = state[*target].referent.value_or(*target);
        // A span local handed on by mutable reference could be made to view
        // other storage than the one its generation is followed for.
        if (const std::optional<Local::Sequence>& handed = state[storage].sequence;
            handed.has_value() && handed->views.has_value()) {
            return reject("span '" + state[storage].spelling +
                          "' is passed by mutable reference; a view is modeled only as a value");
        }
        positions.push_back(Position{offset + static_cast<std::uint32_t>(index), storage, source::may_write(passing)});
    }
    for (const Position& position : positions) {
        if (!position.writable && !unsafe_callee) {
            continue;
        }
        const Local& handed = state[position.storage];
        // A refined element type is a content invariant of the local's
        // storage, and a callee holding the container by mutable reference,
        // or by any reference with unsafe code that may write through it,
        // may leave any value in any element (SPEC.md STDMODEL-020).
        if (handed.sequence.has_value() && !handed.sequence->element.refinements.empty()) {
            return reject("'" + handed.spelling + "' is passed to '" + qualified_name_of(callee) +
                          "' by mutable reference, and its elements must satisfy '" +
                          handed.sequence->element.refinements.front().name +
                          "'; nothing obliges the callee to leave only such values in it, so a container "
                          "whose element type is refined is not handed to a call that may write it (SPEC.md "
                          "STDMODEL-020)");
        }
        // The same element reached through a reference and through a view
        // or data pointer of its container would have two post-states.
        if (position.writable && handed.formed_at.has_value()) {
            const std::size_t owner = handed.formed_at->root;
            for (const std::size_t root : written_roots) {
                if (root == owner || may_alias(state[root], state[owner])) {
                    return reject("'" + handed.spelling + "', an element of '" + state[owner].spelling +
                                  "', is passed to '" + qualified_name_of(callee) +
                                  "' by mutable reference, and the same call hands it a view or data pointer "
                                  "through which it may write the elements of '" +
                                  state[root].spelling +
                                  "'; the callee could write that element through either argument, and one "
                                  "storage written through two arguments of a call has no single post-state "
                                  "(SPEC.md STDMODEL-017)");
                }
            }
        }
    }
    // An element handed by reference beside its container handed by mutable
    // reference: the callee may reallocate the container and end the
    // element's lifetime while it still holds the reference.
    for (const Position& element : positions) {
        const Local& handed = state[element.storage];
        if (!handed.formed_at.has_value()) {
            continue;
        }
        const std::size_t owner = handed.formed_at->root;
        for (const Position& container : positions) {
            const Local& holder = state[container.storage];
            if (!(container.writable || unsafe_callee) || holder.formed_at.has_value() ||
                (container.storage != owner && !may_alias(holder, state[owner]))) {
                continue;
            }
            return reject("'" + handed.spelling + "', an element of '" + state[owner].spelling + "', is passed to '" +
                          qualified_name_of(callee) +
                          "' by reference, and the same call "
                          "passes '" +
                          holder.spelling +
                          "' by a reference through which the callee may reallocate it and end that element's "
                          "lifetime; the storage a reference designates must not be storage the callee can "
                          "replace (SPEC.md STDMODEL-016)");
        }
    }
    // A pointer to non-const lets the callee write storage this call does
    // not name, so nothing it reads by reference is known to be preserved.
    const bool through_pointer = writes_through_pointer;
    std::vector<std::size_t> targets;
    std::vector<std::size_t> written_storage;
    for (const Position& position : positions) {
        if (position.writable || unsafe_callee) {
            written_storage.push_back(position.storage);
        }
    }
    // A struct the callee may write may have any of its places written, which
    // reaches whatever may be one of them.
    aggregates::reach_written(groups, unsafe_callee, written_storage);
    // Whether storage the callee only reads may be storage it writes: the
    // same place, or one the common alias model does not keep apart from a
    // written one (SPEC.md CLASS-011, VERIFIED-031).
    const auto reached_by_a_write = [&](std::size_t storage) {
        return through_pointer || std::ranges::any_of(written_storage, [&](std::size_t written) {
                   return written == storage || may_alias(state[written], state[storage]);
               });
    };
    // Shared actual arguments must share one post-state value.
    const auto target_version = [&](std::size_t storage) {
        if (std::ranges::find(targets, storage) == targets.end()) {
            targets.push_back(storage);
            state[storage].version = next_version++;
            // The call's effect is a write to this storage: the caller owes
            // the place's refinement of the value the callee leaves there,
            // so the new version holds it (SPEC.md REFINE-060, CLASS-011).
            valid_versions.insert(state[storage].version);
            // A container the callee holds by mutable reference may be
            // reallocated there: it has a new storage generation
            // (STDMODEL-015).
            if (std::optional<Local::Sequence>& held = state[storage].sequence; held.has_value()) {
                held->invalidated = "passing it by mutable reference to '" + qualified_name_of(callee) + "' at " +
                                    describe_location(cursor);
                library_models.insert(held->kind);
            }
        }
        return state[storage].version;
    };
    // Storage the callee writes takes a post-call version, and so does
    // storage it only reads that may be storage it writes: one storage has
    // one post-call version, however many positions name it. Storage it
    // only reads that no write can reach keeps its version, and the
    // callee's contract describes it at the value it had.
    for (const Position& position : positions) {
        if (!position.writable && !reached_by_a_write(position.storage)) {
            continue;
        }
        const std::uint32_t version = target_version(position.storage);
        call->effects.push_back(CallEffect{position.argument, version, state[position.storage].type});
    }
    // A struct handed by reference leaves one post-state value, of which only
    // the callee's postcondition is supposed (TRUST.md TCB-AGGREGATE-001).
    const aggregates::GroupCall struct_call{cursor, callee, unsafe_callee, &groups, &targets, reached_by_a_write};
    if (StructHooks hooks(*this); !aggregates::post_states(hooks, struct_call, state, invalidated, call->effects)) {
        return std::nullopt;
    }
    // Every other place of the object may have been written as well: the
    // callee's leaves are the storage its contract speaks of, and whatever
    // else the object holds -- an element formed at a term, a member the
    // callee does not track -- is unknown after the call rather than kept
    // (SPEC.md CLASS-011).
    if (object.has_value() && callee_receiver.has_value() &&
        (callee_receiver->writes() || through_pointer || unsafe_callee)) {
        for (std::size_t other = 0; other < state.size(); ++other) {
            const Local& entry = state[other];
            if (entry.referent || entry.is_deref() != object->through_pointer ||
                std::ranges::find(targets, other) != targets.end() ||
                clang_equalCursors(entry.declaration, object->declaration) == 0 ||
                entry.path.size() < object->path.size() ||
                !std::equal(object->path.begin(), object->path.end(), entry.path.begin())) {
                continue;
            }
            state[other].version = next_version++;
            invalidated.push_back(other);
        }
    }
    // Whatever the common alias model does not keep apart from storage the
    // callee writes is unknown after the call (SPEC.md CLASS-010,
    // VERIFIED-030). No argument about types is used: two places are kept
    // apart only where Clang resolves them to distinct storage.
    for (std::size_t other = 0; other < state.size(); ++other) {
        if (state[other].referent || std::ranges::find(targets, other) != targets.end() ||
            std::ranges::find(invalidated, other) != invalidated.end())
            continue;
        if (std::ranges::any_of(written_storage,
                                [&](std::size_t written) { return may_alias(state[written], state[other]); })) {
            state[other].version = next_version++;
            invalidated.push_back(other);
        }
    }
    // A callee holding a container by mutable reference may write any of
    // its elements, and may reallocate it: whatever may be that container or
    // one of its elements is unknown after the call (RFC 0020 §3, §4). A
    // container that may be it has a new storage generation for the same
    // reason, which a stale view of it names, whether or not the loop above
    // already gave it its post-call version (STDMODEL-015).
    for (const std::size_t target : targets) {
        const auto& held = state[target].sequence;
        if (!held.has_value()) {
            continue;
        }
        const auto invalidated_by = held->invalidated;
        for (std::size_t other = 0; other < state.size(); ++other) {
            if (other == target || state[other].referent.has_value() ||
                std::ranges::find(targets, other) != targets.end() || !may_alias(state[target], state[other])) {
                continue;
            }
            if (auto& sequence = state[other].sequence; sequence.has_value()) {
                sequence->invalidated = invalidated_by;
            }
            if (std::ranges::find(invalidated, other) == invalidated.end()) {
                state[other].version = next_version++;
                invalidated.push_back(other);
            }
        }
    }
    havoc_pointees(targets);
    return value;
}

std::optional<Expr> BodyLowering::lower_call(CXCursor statement, const Continuation& next, const Locals& locals,
                                             unsigned depth) {
    if (const std::optional<SequenceCall> call = sequence_call(statement)) {
        return lower_sequence_statement(*call, statement, next, locals, depth);
    }
    if (aggregates::assigns_whole(statement)) {
        StructHooks hooks(*this);
        return aggregates::lower_whole_assignment(
            hooks, statement, locals, frame_of(signature),
            [&](const Locals& assigned) { return lower_statements(next, assigned, depth + 1); });
    }
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
        *body = unknown(state, index, std::move(*body), statement);
    return bind(version, anonymous_place("discarded call"), std::move(*value), std::move(*body), statement);
}

} // namespace cppl::clangbridge::detail
