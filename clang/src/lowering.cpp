#include "lowering.hpp"

#include "access.hpp"
#include "aggregate_values.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
#include "ghost.hpp"
#include "places.hpp"
#include "refinements.hpp"
#include "signature.hpp"
#include "types.hpp"
#include "unsafe.hpp"
#include "write_scan.hpp"

#include <algorithm>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// The lowering of a verified body (SPEC.md 12.8): the places it tracks from
// entry and how they enter, the capabilities its contract states, the one
// write, a version nothing describes, and the post-state a normal return hands
// back to the caller.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::take;

namespace {

// Whether a value of `type` states a refinement of an object rather than of a
// scalar: its own, where it is a record or an array, or that of a member or an
// element that is one. Past the depth places are tracked to, the answer is yes.
bool refines_an_object(const Type& type, unsigned depth = 0) {
    if (type.kind != TypeKind::Value) {
        return false;
    }
    if (!type.refinements.empty() || depth > kMaxPlaceDepth) {
        return true;
    }
    return std::ranges::any_of(type.projections,
                               [depth](const Type& component) { return refines_an_object(component, depth + 1); });
}

} // namespace

std::size_t return_paths(const Expr& expression) {
    if (const auto* branch = std::get_if<Conditional>(&expression.node)) {
        return return_paths(branch->operands[1]) + return_paths(branch->operands[2]);
    }
    if (const auto* bound = std::get_if<PlaceVersion>(&expression.node)) {
        return return_paths(bound->operands[1]);
    }
    if (const auto* loop = std::get_if<Loop>(&expression.node)) {
        return return_paths(loop->operands.back());
    }
    if (const auto* unknown = std::get_if<UnknownVersion>(&expression.node); unknown && unknown->operands.size() == 1)
        return return_paths(unknown->operands.front());
    if (const auto* region = std::get_if<UnsafeRegion>(&expression.node); region && region->operands.size() == 1)
        return return_paths(region->operands.front());
    return 1;
}

// Whether the value bound for a newly formed place is known to inhabit the
// place's declared type, so the walk may state that type's refinement of it
// (SPEC.md REFINE-060).
//
// The rule needs closed accounting for every write that may reach the
// place: validity for the version it entered the modeled state with, and
// the same predicate charged at every write since. Where that holds, the
// element's current version holds a value of its type even though which
// element it is stays undecided.
//
// What follows is where *this* implementation has that accounting, not the
// limit of where it could be had. Indirection does not disqualify a place
// in principle; it disqualifies it here because nothing closes the
// accounting behind a pointer, whose declared pointee type is erased and so
// is no evidence at all about what the pointee holds. The same goes for
// storage a reference parameter designates, which the caller may write
// through another reference, and for a local whose address escaped, which a
// write this body never modeled can reach. Widening any of these means
// establishing the accounting first, never relaxing the test.
bool BodyLowering::confined_element(const Local& entry) const {
    return entry.symbolic && !entry.is_deref() && !entry.external &&
           !unconfined.contains(clang_hashCursor(entry.declaration));
}

// Wrap `body` in an opaque binding for each dereference place formed while
// the statement was lowered, outermost first so each version is bound
// before anything reads it.
Expr BodyLowering::bind_formed_derefs(Expr body, CXCursor at) {
    for (const Local& entry : std::ranges::reverse_view(formed_derefs)) {
        Locals one{entry};
        body = unknown(one, 0, std::move(body), at, confined_element(entry));
        // A symbolic element owes `index < extent` where it was formed. The
        // bound wraps the binding, so the obligation stands whether or not
        // the element's value is ever used.
        if (entry.symbolic && !entry.index_value.empty() && !entry.extent.empty()) {
            Expr bound;
            bound.type = body.type;
            bound.location = entry.index_value.front().location.is_valid()
                                 ? entry.index_value.front().location
                                 : presumed_location(clang_getCursorLocation(at));
            bound.node = ElementBound{entry.extent, {entry.index_value.front(), std::move(body)}};
            body = std::move(bound);
        }
    }
    formed_derefs.clear();
    return body;
}

// Whether the contract grants `kind` on the pointee of the pointer held in
// `parameter`. `writable` does not entail `readable` and `readable` does
// not entail `writable`: an output buffer may be written and not read
// (RFC 0014 §3).
const StatedCapability* BodyLowering::granted_capability(std::uint32_t parameter, Capability::Kind kind) const {
    if (capabilities == nullptr || revoked_by.has_value()) {
        return nullptr;
    }
    const auto at = std::ranges::find_if(*capabilities, [&](const StatedCapability& stated) {
        return stated.parameter == parameter && stated.kind == kind;
    });
    return at == capabilities->end() ? nullptr : &*at;
}

bool BodyLowering::granted(std::uint32_t parameter, Capability::Kind kind) const {
    return granted_capability(parameter, kind) != nullptr;
}

// Why dereferencing pointer parameter `spelling` is refused for want of the
// capability `required`.
std::string BodyLowering::capability_refusal(const std::string& spelling, Capability::Kind required) const {
    const bool writing = required == Capability::Kind::Writable;
    return std::string(writing ? "writing through '" : "reading '") + spelling + "' requires '" +
           (writing ? "writable(" : "readable(") + spelling + ")', " +
           (revoked_by.has_value()
                ? "which no longer holds after the unsafe block at " + revoked_by->file + ":" +
                      std::to_string(revoked_by->line) + ": what that block did to the storage was not checked"
                : "which was not established; 'p != nullptr' does not imply it");
}

// Whether a normal return states the storage the caller can see afterwards:
// a void function's, and one taking a parameter by reference. A member
// function's implicit object is such storage whenever it has a leaf, since
// each leaf is a reference parameter (SPEC.md CLASS-008).
bool BodyLowering::has_post_state() const {
    return executable_state && (result_type.kind == TypeKind::Void || signature.leaves() != 0 ||
                                std::ranges::any_of(parameters, [](CXCursor parameter) {
                                    return source::aliases_storage(passing_of(clang_getCursorType(parameter)));
                                }));
}

Expr BodyLowering::completed(Expr value, const Locals& locals, CXCursor at) {
    if (!has_post_state())
        return value;
    ReturnState state;
    state.operands.push_back(std::move(value));
    // The places the caller sees again whose current version's refinement
    // validity was never established: those a normal return is charged
    // (SPEC.md CLASS-010, REFINE-061). Every other version was charged
    // where it was established, and is not charged again here (REFINE-062).
    std::vector<std::size_t> unestablished;
    const auto returned = [&](std::size_t local) {
        if (!valid_versions.contains(locals[local].version) && carries_refinement(locals[local].type)) {
            unestablished.push_back(local);
        }
        state.operands.push_back(read_place(locals, local, at));
    };
    // The implicit object's leaves first, each at the version current where
    // the function returns: the post-state its postcondition describes
    // (SPEC.md CONTRACT-009). Every leaf is tracked from entry.
    if (signature.receiver.has_value()) {
        for (const ReceiverLeaf& leaf : signature.receiver->leaves) {
            const auto local = find_local(locals, signature.receiver->record, leaf.path);
            if (!local) {
                return unsupported_expression(at, "the implicit object's '" + leaf.spelling +
                                                      "' is not tracked where the function returns");
            }
            returned(*local);
        }
    }
    // The object a reference parameter designates, followed member by member,
    // is handed back as the value its leaves assemble: bound where the return
    // stands, of which its members' values are all that is supposed, as of any
    // value a body assembles (TRUST.md TCB-AGGREGATE-001, TCB-AGGREGATE-003).
    struct AssembledState {
        std::uint32_t version = 0;
        Expr value;
        std::string spelling;
    };
    std::vector<AssembledState> assembled_states;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const auto local = find_local(locals, parameters[index]);
        const bool aliases = source::aliases_storage(passing_of(clang_getCursorType(parameters[index])));
        std::optional<Expr> designated =
            aliases && !local ? aggregates::designated_value(parameters[index], locals, frame_of(signature), at)
                              : std::nullopt;
        if (local && aliases) {
            returned(*local);
        } else if (designated.has_value()) {
            if (std::holds_alternative<Unsupported>(designated->node)) {
                return std::move(*designated);
            }
            // Each leaf is charged its refinement where its version was not
            // established, exactly as a scalar the caller sees again is.
            for (std::size_t leaf = 0; leaf < locals.size(); ++leaf) {
                if (clang_equalCursors(locals[leaf].declaration, parameters[index]) != 0 &&
                    !locals[leaf].referent.has_value() && !locals[leaf].has_symbolic_step() &&
                    !valid_versions.contains(locals[leaf].version) && carries_refinement(locals[leaf].type)) {
                    unestablished.push_back(leaf);
                }
            }
            AssembledState assembled{next_version++, std::move(*designated),
                                     "the post-state of '" + take(clang_getCursorSpelling(parameters[index])) + "'"};
            Expr post;
            post.type = assembled.value.type;
            post.location = presumed_location(clang_getCursorLocation(at));
            post.node = PlaceRef{assembled.version, anonymous_place(assembled.spelling)};
            state.operands.push_back(std::move(post));
            assembled_states.push_back(std::move(assembled));
        } else {
            Expr input;
            input.type = convert_type(clang_getCursorType(parameters[index]), 0, ReferenceModel::Referent);
            input.location = presumed_location(clang_getCursorLocation(at));
            input.node = ParameterRef{signature.position(index), take(clang_getCursorSpelling(parameters[index]))};
            state.operands.push_back(std::move(input));
        }
    }
    Expr result;
    result.type = result_type;
    result.location = clang_Cursor_isNull(at) ? completion_location : presumed_location(clang_getCursorLocation(at));
    result.node = std::move(state);
    // Each such version enters its place's refinement here, where it is
    // handed back to the caller, as a value a write puts in the place does.
    // A function that runs off its end has no return statement to stand
    // at; the charge stands where the function completes.
    const auto where = result.location;
    for (AssembledState& assembled : std::ranges::reverse_view(assembled_states)) {
        result = bind(assembled.version, anonymous_place(assembled.spelling), std::move(assembled.value),
                      std::move(result), at, {});
        result.location = where;
    }
    for (const std::size_t local : std::ranges::reverse_view(unestablished)) {
        Expr handed_back = read_place(locals, local, at);
        handed_back.location = where;
        result = bind(next_version++, anonymous_place("the post-state of '" + locals[local].spelling + "'"),
                      std::move(handed_back), std::move(result), at, locals[local].type);
        result.location = where;
    }
    return result;
}

Expr BodyLowering::void_value(CXCursor at) const {
    Expr value;
    value.type = result_type;
    value.location = clang_Cursor_isNull(at) ? completion_location : presumed_location(clang_getCursorLocation(at));
    value.node = IntLiteral{0};
    return value;
}

// Havoc uses the same version namespace as exact writes. No premise is
// inherited for the new value; old facts still name only old versions.
Expr BodyLowering::unknown(const Locals& state, std::size_t entry, Expr body, CXCursor at, bool confined) {
    // A leaf of a struct a call handed by reference and may have written
    // holds its member of the struct's post-state value, so it is bound to
    // that value rather than left unknown. Its declared type is charged where
    // the binding stands, as any write's is (TRUST.md TCB-AGGREGATE-001).
    if (const auto rebound = rebound_leaves.find(state[entry].version); rebound != rebound_leaves.end()) {
        return bind(state[entry].version, place_of(state, entry), rebound->second, std::move(body), at,
                    state[entry].type);
    }
    Expr result;
    result.type = body.type;
    result.location = presumed_location(clang_getCursorLocation(at));
    result.node =
        UnknownVersion{state[entry].version, place_of(state, entry), state[entry].type, {std::move(body)}, confined};
    return result;
}

std::nullopt_t BodyLowering::reject(std::string reason) {
    if (rejection.empty()) {
        rejection = std::move(reason);
    }
    return std::nullopt;
}

// The one write of tracked storage (SPEC.md 12.10, RFC 0014 §5).
//
// Establishing a version is what a write is, whatever syntax performed it:
// a declaration, an assignment, a compound update, a member
// initialization, a call's effect on an argument. `declared` is the type
// the place was written with, and it is what the refinement crossing is
// generated from downstream, at one site rather than per form.
Expr BodyLowering::bind(std::uint32_t version, Place place, Expr value, Expr body, CXCursor at, Type declared) {
    Expr expr;
    expr.type = body.type;
    expr.location = presumed_location(clang_getCursorLocation(at));
    expr.node = PlaceVersion{version, std::move(place), {std::move(value), std::move(body)}, std::move(declared)};
    return expr;
}

// A place that is not tracked storage: a call result or another value the
// body binds without naming storage. It has a version so the value is
// stated once, and a spelling so diagnostics can name it.
Place BodyLowering::anonymous_place(std::string spelling) {
    Place place;
    place.root.kind = PlaceRoot::Kind::Local;
    place.root.id = std::numeric_limits<std::uint32_t>::max();
    place.spelling = std::move(spelling);
    return place;
}

void extract_body(Function& function, CXCursor cursor, const Signature& signature, const std::string& invariant_prefix,
                  const std::vector<Selection::Refinement>& refinements, bool executable_state,
                  const std::vector<StatedCapability>* capabilities, UnsafeEffects& unsafe_effects) {
    const std::vector<CXCursor>& parameters = signature.parameters;
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
    BodyLowering lowering{.signature = signature,
                          .parameters = parameters,
                          .result_type = function.result,
                          .invariant_prefix = invariant_prefix,
                          .refinements = &refinements,
                          .unsafe_effects = &unsafe_effects,
                          .executable_state = executable_state,
                          .capabilities = capabilities};
    lowering.completion_location = presumed_location(clang_getRangeEnd(clang_getCursorExtent(members[body_index])));
    lowering.escaped = escaped_locals(members[body_index]);
    lowering.unconfined = unconfined_locals(members[body_index]);
    // Whatever an unsafe block names, it may write, and it may keep the address
    // and write through it later, from another unsafe block that never names it.
    // So every such name is treated as escaped, which is what makes each unsafe
    // block reach it, and as unconfined, since the writes there owed no
    // refinement (SPEC.md UNSAFE-003, TRUST.md TCB-UNSAFE-002).
    const std::vector<CXCursor> unsafe_blocks = unsafe_blocks_in(members[body_index], invariant_prefix);
    for (const CXCursor block : unsafe_blocks) {
        for (const unsigned named : named_declarations(block)) {
            lowering.escaped.insert(named);
            lowering.unconfined.insert(named);
        }
    }
    // Ghost state is decided for the whole body before any path is lowered: a
    // use by code that runs is an error wherever it stands, reached or not.
    if (!invariant_prefix.empty()) {
        GhostScan ghosts(invariant_prefix);
        ghosts.run(members[body_index]);
        function.ghost_calls = std::move(ghosts.calls);
        if (!ghosts.errors.empty()) {
            function.ghost_errors = std::move(ghosts.errors);
            function.body_rejection = "it declares or uses ghost state in a way the language does not allow";
            return;
        }
    }
    // A view cannot be returned: it would outlive every storage this body could
    // have formed it over, or designate caller storage no contract says stays
    // valid (SPEC.md STDMODEL-015, RFC 0020 §5).
    if (executable_state && function.result.representation.kind == source::RepresentationKind::Span) {
        function.body_rejection = "it returns a span, and a view is not returned: it could outlive the storage it "
                                  "views (SPEC.md STDMODEL-015)";
        return;
    }
    if (executable_state && source::is_sequence(function.result.representation.kind)) {
        // A refinement written as the element type states a content invariant
        // of a local's storage and nothing of the C++ type, which is the base
        // type's specialization, so a result spelling one promises what no
        // caller receives (SPEC.md STDMODEL-020).
        auto element = sequence_element(cursor, clang_getCursorResultType(cursor), &refinements);
        if (!element) {
            function.body_rejection = "its result: " + element.error();
            return;
        }
        if (const bool refined_result = !element->refinements.empty(); refined_result) {
            function.body_rejection = "its result is a container whose element type is written as the refinement '" +
                                      element->refinements.front().name +
                                      "'; a refined element type states a content invariant of a local and is "
                                      "modeled only there (SPEC.md STDMODEL-020)";
            return;
        }
        lowering.library_models.insert(function.result.representation.kind);
    }
    Locals candidates;
    // A member function's implicit object is caller storage, one place per
    // leaf, rooted in the object's class: distinct members of it are distinct
    // storage, and any of them may be what a reference parameter designates
    // (SPEC.md CLASS-008, CLASS-010).
    if (executable_state && signature.receiver.has_value()) {
        for (const ReceiverLeaf& leaf : signature.receiver->leaves) {
            candidates.push_back(Local{.declaration = signature.receiver->record,
                                       .type = leaf.type,
                                       .external = true,
                                       .path = leaf.path,
                                       .spelling = leaf.spelling});
        }
    }
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (!executable_state) {
            continue;
        }
        const auto& parameter = function.parameters[signature.position(index)];
        // A vector or string parameter is the root of a modeled sequence: its
        // versions carry its length, and its element places are formed on
        // access. A span parameter is not tracked: it is a value the call
        // fixed, whose elements are caller storage reached under a capability
        // (RFC 0020 §3, §7).
        if (source::is_sequence(parameter.type.representation.kind)) {
            const std::string name = take(clang_getCursorSpelling(parameters[index]));
            if (!parameter.type.representation.rejection.empty()) {
                function.body_rejection = "parameter '" + name + "' has type '" + parameter.type.spelling +
                                          "', which is not modeled: " + parameter.type.representation.rejection;
                return;
            }
            auto element = sequence_element(parameters[index], clang_getCursorType(parameters[index]), &refinements);
            if (!element) {
                function.body_rejection = "parameter '" + name + "': " + element.error();
                return;
            }
            // No caller proof could establish that every element of a
            // container it passes satisfies a refinement, and an unverified
            // caller passes the same C++ type (SPEC.md STDMODEL-020).
            if (!element->refinements.empty()) {
                function.body_rejection = "parameter '" + name +
                                          "' is a container of refined elements, whose element validity no call can "
                                          "establish; a refined element type is modeled only for a local "
                                          "(SPEC.md STDMODEL-020)";
                return;
            }
            lowering.library_models.insert(parameter.type.representation.kind);
            if (parameter.type.representation.kind == source::RepresentationKind::Span) {
                if (parameter.passing != source::ParameterPassing::Value) {
                    function.body_rejection = "span parameter '" + name + "' is modeled only by value";
                    return;
                }
                continue;
            }
            Local root{.declaration = parameters[index],
                       .type = parameter.type,
                       .external = source::aliases_storage(parameter.passing),
                       .spelling = name};
            root.sequence = Local::Sequence{};
            root.sequence->kind = parameter.type.representation.kind;
            root.sequence->element = std::move(*element);
            root.sequence->external_elements = source::aliases_storage(parameter.passing);
            candidates.push_back(std::move(root));
            continue;
        }
        if (parameter.type.kind == TypeKind::Int || parameter.type.kind == TypeKind::Bool) {
            candidates.push_back(Local{.declaration = parameters[index],
                                       .type = parameter.type,
                                       .external = source::aliases_storage(parameter.passing),
                                       .spelling = take(clang_getCursorSpelling(parameters[index]))});
            continue;
        }
        // A by-value aggregate parameter is the callee's own copy of the
        // caller's value, so its members are ordinary storage of this body and
        // writing one has the same modeled effect as writing a local's member.
        // A parameter that aliases caller storage is not: another reference may
        // designate the same object, so what a write there reaches is not
        // decided here (RFC 0014 §4).
        //
        // A type whose members cannot all be enumerated is left untracked, and
        // a write to it is refused where it is written, as before.
        if (parameter.type.kind == TypeKind::Value && !source::aliases_storage(parameter.passing)) {
            std::vector<BodyLowering::AggregateLeaf> leaves;
            const std::string name = take(clang_getCursorSpelling(parameters[index]));
            if (!lowering.collect_type_leaves(parameter.type, name, {}, leaves)) {
                for (BodyLowering::AggregateLeaf& leaf : leaves) {
                    candidates.push_back(Local{.declaration = parameters[index],
                                               .type = leaf.type,
                                               .path = std::move(leaf.path),
                                               .spelling = std::move(leaf.spelling)});
                }
            }
            continue;
        }
        // An object a parameter designates by reference is caller storage
        // another reference may reach, so it is tracked as one whole place
        // whose version any write that may alias it replaces: a member read
        // after such a write projects a value nothing states, never the one
        // the parameter arrived with (SPEC.md 12.9, CLASS-010). A pointer is
        // left as it was: what it designates is a dereference place of its own.
        //
        // A record or an array whose every member is modeled is followed member
        // by member instead, as the implicit object is: one place per scalar
        // leaf, each caller storage the common alias model relates to every
        // other reference, so a write to one member leaves the others, and a
        // member call or a call handed the object takes each leaf's effect. A
        // normal return hands back the value its leaves assemble (TRUST.md
        // TCB-AGGREGATE-003). An object whose own type, or a nested member's,
        // states a refinement keeps one place: its predicate is charged of the
        // whole, which leaf-by-leaf writes would never be.
        if (source::aliases_storage(parameter.passing) && parameter.type.kind == TypeKind::Value &&
            aggregates::structural(parameter.type) && !refines_an_object(parameter.type)) {
            std::vector<BodyLowering::AggregateLeaf> leaves;
            const std::string name = take(clang_getCursorSpelling(parameters[index]));
            if (!lowering.collect_type_leaves(parameter.type, name, {}, leaves)) {
                for (BodyLowering::AggregateLeaf& leaf : leaves) {
                    Local member{.declaration = parameters[index],
                                 .type = leaf.type,
                                 .path = std::move(leaf.path),
                                 .spelling = std::move(leaf.spelling)};
                    member.external = true;
                    candidates.push_back(std::move(member));
                }
                continue;
            }
        }
        if (parameter.type.kind == TypeKind::Value && source::aliases_storage(parameter.passing) &&
            parameter.type.representation.kind != source::RepresentationKind::Pointer) {
            candidates.push_back(Local{.declaration = parameters[index],
                                       .type = parameter.type,
                                       .external = true,
                                       .spelling = take(clang_getCursorSpelling(parameters[index])),
                                       .read_only = true});
        }
    }
    // A parameter this body does not track has one value throughout it, which
    // an unsafe block could change without the change being seen. Such a block
    // is refused rather than followed by a stale value (SPEC.md UNSAFE-005).
    for (const CXCursor parameter : parameters) {
        const bool tracked = std::ranges::any_of(candidates, [&](const Local& candidate) {
            return clang_equalCursors(candidate.declaration, parameter) != 0;
        });
        if (tracked) {
            continue;
        }
        for (const CXCursor block : unsafe_blocks) {
            if (may_rebind(block, parameter)) {
                const source::SourceLocation at = presumed_location(clang_getCursorLocation(block));
                function.body_rejection = "the unsafe block at " + at.file + ":" + std::to_string(at.line) +
                                          " may change parameter '" + take(clang_getCursorSpelling(parameter)) +
                                          "' itself, which this body does not track, so what it holds afterwards "
                                          "could not be followed";
                return;
            }
        }
    }
    // Every tracked parameter is followed where an unsafe block may reach it.
    std::vector<bool> needed(candidates.size(), lowering.has_post_state() || !unsafe_blocks.empty());
    // A container parameter is always followed: its length is read through its
    // root wherever the body names it.
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (candidates[index].sequence.has_value()) {
            needed[index] = true;
        }
    }
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
        // The version a place enters the body with holds its refinement: the
        // caller established it where the value entered the type (SPEC.md
        // REFINE-007, CLASS-010), and the contract supposes it on entry.
        lowering.valid_versions.insert(candidates[index].version);
        entry.push_back(candidates[index]);
    }
    function.returned_value = lowering.lower_statements(Continuation{nullptr, &statements, 0}, entry, 0);
    if (function.returned_value) {
        for (std::size_t index = entry.size(); index > 0; --index) {
            const Local& local = entry[index - 1];
            // A leaf of the implicit object enters as the parameter it is.
            if (signature.receiver.has_value() &&
                clang_equalCursors(local.declaration, signature.receiver->record) != 0) {
                const std::optional<std::size_t> leaf = signature.receiver->leaf_at(local.path);
                if (!leaf.has_value()) {
                    function.returned_value.reset();
                    lowering.rejection = "the implicit object's '" + local.spelling + "' is not one of its leaves";
                    break;
                }
                Expr value;
                value.type = function.parameters[*leaf].type;
                value.location = function.location;
                value.node = ParameterRef{static_cast<std::uint32_t>(*leaf), local.spelling};
                *function.returned_value = lowering.bind(local.version, place_of(entry, index - 1), std::move(value),
                                                         std::move(*function.returned_value), cursor);
                continue;
            }
            const std::optional<std::uint32_t> position = signature.position_of(local.declaration);
            if (!position.has_value()) {
                function.returned_value.reset();
                lowering.rejection = "'" + local.spelling + "' is not a parameter of this body";
                break;
            }
            Expr value;
            value.type = function.parameters[*position].type;
            value.location = function.location;
            value.node = ParameterRef{*position, take(clang_getCursorSpelling(local.declaration))};
            // A member entry holds a projection of the parameter's value rather
            // than the whole of it: the parameter arrives as one value, and its
            // members are the places inside that value.
            bool projected = true;
            for (const PlaceStep& step : local.path) {
                if (step.index >= value.type.projections.size()) {
                    projected = false;
                    break;
                }
                Expr component;
                component.type = value.type.projections[step.index];
                component.location = value.location;
                component.node = Projection{step.index, {std::move(value)}};
                value = std::move(component);
            }
            if (!projected) {
                function.returned_value.reset();
                lowering.rejection = "parameter '" + take(clang_getCursorSpelling(local.declaration)) +
                                     "' has a member this implementation cannot state as a projection of it";
                break;
            }
            *function.returned_value = lowering.bind(local.version, place_of(entry, index - 1), std::move(value),
                                                     std::move(*function.returned_value), cursor);
        }
    }
    if (!function.returned_value) {
        function.body_rejection = lowering.rejection.empty() ? "every path must return a value" : lowering.rejection;
    }
    function.loop_invariants = std::move(lowering.consumed_invariants);
    function.path_contradictions = std::move(lowering.consumed_contradictions);
    function.path_splits = std::move(lowering.consumed_splits);
    function.unsafe_regions = std::move(lowering.consumed_unsafe);
    function.library_models.assign(lowering.library_models.begin(), lowering.library_models.end());
}

} // namespace cppl::clangbridge::detail
