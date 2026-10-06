#include "access.hpp"
#include "call_objects.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
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
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// The places a statement forms (RFC 0014 §1, §17, RFC 0020 §3): a dereference,
// under the capability its contract states for the pointer, an element selected
// at a term, owing its bound, and a container's element at its storage
// generation; each recorded for the statement to bind before anything reads it.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::strip_parens;
using bridge::take;

// Resolve an access to the entry holding the storage it names, forming a
// dereference place when it goes through a pointer (RFC 0014 §1, §17
// steps 5-6).
//
// This is the single point where a pointer becomes a place, so the
// capability obligation is owed here and cannot be bypassed by choosing a
// different syntax: `*p`, `p->m` and `p[i]` all arrive here. The capability
// must already be in scope; nothing about the pointer's value establishes
// it, and it is never assumed because the access needed it (SPEC.md
// VERIFIED-037, VERIFIED-043).
//
// `required` is the capability the access needs: reading requires
// `readable`, writing requires `writable`, and neither entails the other.
std::optional<std::size_t> BodyLowering::resolve_storage(CXCursor cursor, Locals& state, Capability::Kind required) {
    const auto access = resolve_access(cursor);
    if (!access) {
        return std::nullopt;
    }
    const auto declaration = access->declaration;
    if (!access->dereferenced) {
        return find_local(state, declaration, access->path);
    }
    // The pointer must be a parameter the contract can name, because a
    // capability is stated about a parameter. A pointer that is a local has
    // no stated capability and no way to earn one yet, so it fails closed.
    //
    // The pointer's own storage need not be tracked: what is tracked is the
    // pointee place. A pointer parameter is not a modeled value here, and a
    // write to the pointer itself is refused elsewhere, so the version that
    // identifies the pointee is the pointer's initial one.
    const auto at = std::ranges::find_if(
        parameters, [&](CXCursor candidate) { return clang_equalCursors(candidate, declaration) != 0; });
    const auto pointer = find_local(state, declaration);
    const std::size_t root =
        pointer.value_or(at == parameters.end() ? std::size_t{0} : static_cast<std::size_t>(at - parameters.begin()));
    const std::uint32_t version = pointer ? state[*pointer].version : 0;
    // A symbolic subscript is identified by its index term as well as by its
    // path, so `p[i]` and `p[j]` are two places. The term is read here, at
    // the versions current before this access forms anything, which is the
    // same state it would be read in below.
    std::optional<Expr> selected_index;
    if (!access->path.empty() && access->path.back().kind == PlaceStep::Kind::SymbolicElement &&
        !access->symbolic_indices.empty()) {
        selected_index = build_expression(access->symbolic_indices.back(), signature, state, 0);
    }
    if (auto existing = find_deref(state, root, version, access->path, selected_index ? &*selected_index : nullptr);
        existing.has_value()) {
        // A place formed earlier is reached again only under the capability
        // this access needs, still held here (SPEC.md VERIFIED-038): a write
        // needs `writable` even where a read formed the place, a read needs
        // `readable` even where a write formed it, and no capability
        // survives an unsafe block (VERIFIED-043). The capability names the
        // pointer by its callable position (SPEC.md CLASS-008).
        const bool held = at != parameters.end() &&
                          granted(signature.position(static_cast<std::size_t>(at - parameters.begin())), required);
        if (!held) {
            rejection = capability_refusal(take(clang_getCursorSpelling(declaration)), required);
            return std::nullopt;
        }
        return existing;
    }
    if (at == parameters.end()) {
        rejection = "dereferencing '" + take(clang_getCursorSpelling(declaration)) +
                    "' requires a memory capability, and only a pointer parameter named by an expects clause "
                    "can carry one";
        return std::nullopt;
    }
    // A capability names the pointer by its callable position, which is
    // past a member function's implicit object (SPEC.md CLASS-008).
    const auto index = signature.position(static_cast<std::size_t>(at - parameters.begin()));
    if (!granted(index, required)) {
        rejection = capability_refusal(take(clang_getCursorSpelling(declaration)), required);
        return std::nullopt;
    }
    // A capability permits reaching the pointer's storage; it does not say
    // which element of that storage a subscript names. The two are separate
    // obligations and stay separate: the capability is tracked as a context
    // hypothesis, while `index < extent` is a proposition about values that
    // the kernel proves (RFC 0014 §10, §17 step 7, SPEC.md VERIFIED-038).
    //
    // Only the sized form states an extent. `readable(p)` describes one
    // object, so it reaches no element beyond the first and there is no
    // bound to compare against; the access fails closed rather than
    // treating an unstated extent as an unbounded one (VERIFIED-043).
    const bool subscripted = std::ranges::any_of(access->path, [](const PlaceStep& step) {
        return step.kind == PlaceStep::Kind::Element || step.kind == PlaceStep::Kind::SymbolicElement;
    });
    const StatedCapability* stated = granted_capability(index, required);
    std::vector<Expr> element_extent;
    if (subscripted) {
        if (stated == nullptr || stated->extent.empty()) {
            rejection = "subscripting '" + take(clang_getCursorSpelling(declaration)) + "' requires '" +
                        (required == Capability::Kind::Writable ? "writable(" : "readable(") +
                        take(clang_getCursorSpelling(declaration)) +
                        ", n)' to state the extent its index must lie within; the one-object form bounds no "
                        "element";
            return std::nullopt;
        }
        element_extent = stated->extent;
    }
    // The pointee type is what the pointer points to, with its sugar kept
    // so a refinement named on the pointee is still known.
    CXType pointee = clang_getPointeeType(clang_getCursorType(declaration));
    // A member of the object the pointer designates, `p->m` or
    // `(*p).a.b`, is a place of that member's own type, with the
    // refinement its declaration names (SPEC.md CLASS-011, 17.6).
    CXCursor declared_by = declaration;
    if (!access->path.empty() && !subscripted) {
        const CXCursor field = clang_getCursorReferenced(strip_parens(cursor));
        if (clang_getCursorKind(field) != CXCursor_FieldDecl) {
            rejection = "this member of what '" + take(clang_getCursorSpelling(declaration)) +
                        "' designates is not one this implementation models";
            return std::nullopt;
        }
        pointee = clang_getCursorType(field);
        declared_by = field;
    }
    Type type = convert_type(pointee, 0, ReferenceModel::Opaque, refinements);
    if (type.kind == TypeKind::Unsupported) {
        rejection = "the pointee of '" + take(clang_getCursorSpelling(declaration)) + "' is not modeled";
        return std::nullopt;
    }
    // A refinement on the pointee is verification-level identity Clang
    // canonicalizes away, so it is recovered from the written type. Without
    // this a write through `Positive*` would owe nothing (SPEC.md 17.3).
    if (refinements != nullptr) {
        auto resolved = refinements_of(declared_by, pointee, *refinements);
        if (!resolved) {
            rejection =
                "the pointee of '" + take(clang_getCursorSpelling(declaration)) + "' has " + resolved.error().message;
            return std::nullopt;
        }
        type.refinements = std::move(*resolved);
    }
    Local entry;
    entry.declaration = declaration;
    entry.version = next_version++;
    entry.type = std::move(type);
    entry.path = access->path;
    entry.pointer = root;
    entry.pointer_version = version;
    entry.spelling = "*" + take(clang_getCursorSpelling(declaration));
    // A subscript through a capability owes `index < n` against the extent
    // the contract stated, whether the index is a term or a constant. The
    // index is lowered here, where the place is formed, so it denotes the
    // versions current at the access.
    if (subscripted) {
        const PlaceStep& last = access->path.back();
        Expr selected;
        if (last.kind == PlaceStep::Kind::SymbolicElement) {
            if (!selected_index) {
                rejection = "this subscript has no index expression to bound";
                return std::nullopt;
            }
            selected = std::move(*selected_index);
        } else {
            // A constant index states the same obligation: `a[999]` owes
            // `999 < n` exactly as `a[i]` owes `i < n`. Nothing about a
            // literal makes it within the extent.
            selected.type = element_extent.front().type;
            selected.location = element_extent.front().location;
            selected.node = IntLiteral{static_cast<std::int64_t>(last.index)};
        }
        entry.symbolic = true;
        entry.index_value.push_back(std::move(selected));
        entry.extent = std::move(element_extent);
    }
    state.push_back(std::move(entry));
    return state.size() - 1;
}

// The resolved type of the storage `prefix` designates within `declaration`.
//
// This answers "what indexed structure does this object have", which is a
// question about its C++ type and not about which of its elements the proof
// has met so far. The place answers "which object" separately
// (ARCHITECTURE.md ARCH-ELEM-004).
Type BodyLowering::declared_place_type(CXCursor declaration, const std::vector<PlaceStep>& prefix,
                                       const Locals& state) const {
    // A tracked entry for the whole object is preferred: it already carries
    // the type the declaration was modeled with, including a reference
    // parameter's referent type.
    for (const Local& candidate : state) {
        if (clang_equalCursors(candidate.declaration, declaration) != 0 && !candidate.symbolic &&
            candidate.path.empty()) {
            return walk_components(candidate.type, prefix);
        }
    }
    if (clang_Cursor_isNull(declaration) != 0) {
        return {};
    }
    return walk_components(convert_type(reference_value_type(clang_getCursorType(declaration))), prefix);
}

// The type each step of `path` selects, by the resolved component order the
// representation already records.
Type BodyLowering::walk_components(Type current, const std::vector<PlaceStep>& path) {
    for (const PlaceStep& step : path) {
        if (step.kind == PlaceStep::Kind::SymbolicElement || step.index >= current.projections.size()) {
            return {};
        }
        current = current.projections[step.index];
    }
    return current;
}

// Form the place a symbolic subscript names, with the bounds obligation it
// owes (RFC 0014 §17 step 7).
//
// The element is undecided, so it gets its own place and an opaque value:
// nothing here decides which element it is. The bounds obligation is a
// proposition about values -- `index < extent` -- so it is proved by the
// kernel rather than tracked as a capability (RFC 0014 §10).
std::optional<std::size_t> BodyLowering::resolve_symbolic_element(Locals& state, const ResolvedAccess& access) {
    const auto declaration = access.declaration;
    if (access.symbolic_indices.empty()) {
        rejection = "this subscript has no index expression to bound";
        return std::nullopt;
    }
    // The index term is read before anything is formed, so an existing place
    // is recognized by the value its index has here rather than by the path
    // alone, which records only that some step was symbolic.
    return symbolic_element_at(state, declaration, access.path,
                               build_expression(access.symbolic_indices.front(), signature, state, 0), access.receiver);
}

// The element place of the array `declaration` holds at `path`, whose last
// step is symbolic, selected by the index term `selected`: one this path
// formed already at that term, or a new one owing `selected < extent`.
std::optional<std::size_t> BodyLowering::symbolic_element_at(Locals& state, CXCursor declaration,
                                                             const std::vector<PlaceStep>& path, Expr selected,
                                                             bool receiver) {
    if (const auto existing = find_symbolic(state, declaration, path, selected); existing.has_value()) {
        return existing;
    }
    // An array local is tracked as one entry per element, so the extent is
    // how many element entries this array has and the element type is
    // theirs. Both come from Clang's resolved layout rather than a separate
    // claim (RFC 0014 §2).
    //
    // The prefix is the path up to the symbolic step; the elements of the
    // array being indexed are the entries sharing it with one more step.
    std::vector<PlaceStep> prefix(path.begin(), path.end() - 1);
    std::uint32_t extent = 0;
    const Type* element = nullptr;
    for (const Local& candidate : state) {
        if (clang_equalCursors(candidate.declaration, declaration) == 0 || candidate.path.size() != prefix.size() + 1 ||
            candidate.symbolic || !std::equal(prefix.begin(), prefix.end(), candidate.path.begin()) ||
            candidate.path.back().kind != PlaceStep::Kind::Element) {
            continue;
        }
        extent = std::max(extent, candidate.path.back().index + 1);
        element = &candidate.type;
    }
    // The extent belongs to the array's resolved type, so it is known
    // before any element of it has been observed. Scanning tracked element
    // entries only ever finds the elements some earlier access happened to
    // form, which would make the array's shape depend on the order of the
    // proof rather than on its C++ type (ARCHITECTURE.md ARCH-ELEM-004).
    Type indexed;
    if (element == nullptr) {
        indexed = declared_place_type(declaration, prefix, state);
        if (indexed.representation.kind == source::RepresentationKind::Array && !indexed.projections.empty()) {
            extent = static_cast<std::uint32_t>(indexed.projections.size());
            element = &indexed.projections.front();
        }
    }
    if (element == nullptr) {
        rejection = "this subscript's array is not tracked storage of this body, so the extent its index must "
                    "lie within is unknown";
        return std::nullopt;
    }
    Local entry;
    entry.declaration = declaration;
    entry.version = next_version++;
    entry.type = *element;
    entry.path = path;
    entry.spelling = receiver ? "this->?[?]" : take(clang_getCursorSpelling(declaration)) + "[?]";
    entry.symbolic = true;
    entry.index_value.push_back(std::move(selected));
    // An element of caller storage -- the implicit object's, above all -- is
    // caller storage too: another reference may reach it, and nothing
    // closes the accounting of its writes here (SPEC.md CLASS-010).
    entry.external = std::ranges::any_of(state, [&](const Local& candidate) {
        return candidate.external && clang_equalCursors(candidate.declaration, declaration) != 0;
    });
    // The obligation compares the index against the extent, so the extent
    // is stated at the index's own type: this array's extent is a count
    // Clang resolved, and it enters the comparison as the literal it is
    // rather than as a separately typed quantity (SPEC.md STORAGE-005).
    Expr count;
    count.type = entry.index_value.front().type;
    count.location = entry.index_value.front().location;
    count.node = IntLiteral{static_cast<std::int64_t>(extent)};
    entry.extent.push_back(std::move(count));
    state.push_back(std::move(entry));
    return state.size() - 1;
}

std::optional<std::size_t> BodyLowering::find_symbolic(const Locals& locals, CXCursor declaration,
                                                       const std::vector<PlaceStep>& path, const Expr& index_value) {
    for (std::size_t index = locals.size(); index > 0; --index) {
        const Local& candidate = locals[index - 1];
        if (candidate.symbolic && clang_equalCursors(candidate.declaration, declaration) != 0 &&
            candidate.path == path && !candidate.index_value.empty() &&
            same_term(candidate.index_value.front(), index_value) && generation_current(locals, candidate)) {
            return index - 1;
        }
    }
    return std::nullopt;
}

// Form, or find, the element place a subscript of a modeled sequence names
// (RFC 0020 §3, SPEC.md STDMODEL-012).
//
// The place belongs to the storage the object owns or views, at that
// storage's current generation, and owes `index < length` where it is
// formed, against the length of the object subscripted: a vector's own, or
// a span's. A span parameter's elements are caller storage reached only
// under the capability the contract states, which is checked on every
// access, not only the first, since an unsafe block revokes it.
std::optional<std::size_t> BodyLowering::resolve_sequence_element(CXCursor cursor, Locals& state,
                                                                  Capability::Kind required) {
    const std::optional<SequenceCall> call = sequence_call(strip_parens(cursor));
    if (!call || call->constructor || call->name != "operator[]" || call->arguments.size() != 1 ||
        !source::is_sequence(call->family)) {
        return reject("this subscript does not name a modeled container element");
    }
    auto region = element_region(call->object, state, signature);
    if (!region) {
        return reject(region.error());
    }
    library_models.insert(call->family);
    if (!element_capability(*region, required)) {
        return std::nullopt;
    }
    const auto access = resolve_access(strip_parens(cursor));
    Expr length = region_length(*region, state, cursor);
    std::optional<Expr> index = access ? element_index(*access, length.type, signature, state) : std::nullopt;
    if (!index) {
        return reject("this subscript's index could not be resolved");
    }
    return sequence_element_at(state, *region, access->path, std::move(*index), std::move(length),
                               take(clang_getCursorSpelling(clang_getCursorReferenced(strip_parens(call->object)))) +
                                   "[...]");
}

// Whether an element of `region` may be reached as `required` asks: a span
// parameter's elements are caller storage, reached only under the
// capability the contract states, which an unsafe block revokes. Refuses,
// naming the capability, where it may not.
bool BodyLowering::element_capability(const ElementRegion& region, Capability::Kind required) {
    if (!region.parameter.has_value() || granted(*region.parameter, required)) {
        return true;
    }
    const std::string spelled = take(clang_getCursorSpelling(region.declaration));
    const std::string kind = required == Capability::Kind::Writable ? "writable(" : "readable(";
    reject(std::string(required == Capability::Kind::Writable ? "writing an element of '" : "reading an element of '") +
           spelled + "' requires '" + kind + spelled + ")', " +
           (revoked_by.has_value()
                ? "which no longer holds after the unsafe block at " + revoked_by->file + ":" +
                      std::to_string(revoked_by->line) + ": what that block did to the storage was not checked"
                : "which was not established: a span does not make the storage it views valid (SPEC.md "
                  "STDMODEL-016)"));
    return false;
}

// The element place of `region` at `path` whose index is the term `index`,
// bounded by `length`: one this path formed already at the current
// generation, or a new one formed there, owing `index < length`.
std::optional<std::size_t> BodyLowering::sequence_element_at(Locals& state, const ElementRegion& region,
                                                             const std::vector<PlaceStep>& path, Expr index,
                                                             Expr length, std::string spelling) {
    if (const auto existing = find_element(state, region, path, index)) {
        return existing;
    }
    Local entry;
    entry.declaration = region.declaration;
    entry.version = next_version++;
    entry.type = region.element;
    entry.path = path;
    entry.spelling = std::move(spelling);
    entry.external = region.external;
    entry.symbolic = true;
    entry.index_value.push_back(std::move(index));
    entry.extent.push_back(std::move(length));
    if (region.root.has_value()) {
        entry.formed_at = Local::Generation{*region.root, state[*region.root].version};
    }
    state.push_back(std::move(entry));
    return state.size() - 1;
}

// Form the places an expression reads -- dereferences and element places
// -- and record each new one, so the statement binds it before anything
// reads it (see `bind_formed_derefs`).
bool BodyLowering::materialize(CXCursor cursor, Locals& state) {
    const std::size_t before = state.size();
    if (!materialize_derefs(cursor, state)) {
        return false;
    }
    for (std::size_t index = before; index < state.size(); ++index) {
        if (state[index].is_deref() || state[index].symbolic) {
            formed_derefs.push_back(state[index]);
        }
    }
    return true;
}

// Whether `cursor` is a subscript of a vector, a string or a span.
bool BodyLowering::is_sequence_subscript(CXCursor cursor) {
    const std::optional<SequenceCall> call = sequence_call(strip_parens(cursor));
    return call && !call->constructor && call->name == "operator[]" && source::is_sequence(call->family);
}

// Form the place of every dereference an expression reads, so the read
// resolves to storage rather than to an opaque value.
//
// A read requires `readable`. The write target is handled separately, by
// `written_local`, because writing requires `writable` and neither
// capability entails the other (RFC 0014 §3).
// Forms the places of the object a member call through a pointer is made on
// (SPEC.md CLASS-011): one dereference place for each place of the
// callee's implicit object, each reached under the capability the contract
// states for the pointer -- `readable` for every place, since the callee
// may read any, and `writable` as well for each it may write
// (VERIFIED-038). A place formed earlier is reached again under the same
// capabilities. Answers whether `call` is such a call, or nothing, with the
// reason in `rejection`, when a place cannot be formed.
std::optional<bool> BodyLowering::form_pointee_receiver(CXCursor call, Locals& state) {
    const CXCursor callee = clang_getCursorReferenced(call);
    if (clang_getCursorKind(callee) != CXCursor_CXXMethod || clang_CXXMethod_isStatic(callee) != 0 ||
        clang_CXXMethod_isVirtual(callee) != 0) {
        return false;
    }
    const auto object = call_object(call, callee);
    if (!object || !object->through_pointer) {
        return false;
    }
    // An object this implementation does not model is refused where the
    // call is lowered, with the reason.
    const auto receiver = receiver_of(callee, refinements);
    const auto root = pointer_root(state, parameters, object->declaration);
    if (!receiver || !root) {
        return false;
    }
    const std::string pointer = take(clang_getCursorSpelling(object->declaration));
    const auto at = std::ranges::find_if(
        parameters, [&](CXCursor candidate) { return clang_equalCursors(candidate, object->declaration) != 0; });
    const std::uint32_t position = signature.position(static_cast<std::size_t>(at - parameters.begin()));
    for (const ReceiverLeaf& leaf : receiver->leaves) {
        std::vector<Capability::Kind> required{Capability::Kind::Readable};
        if (source::may_write(receiver->passing(leaf))) {
            required.push_back(Capability::Kind::Writable);
        }
        for (const Capability::Kind kind : required) {
            if (!granted(position, kind)) {
                rejection = capability_refusal(pointer, kind);
                return std::nullopt;
            }
        }
        std::vector<PlaceStep> path = object->path;
        path.insert(path.end(), leaf.path.begin(), leaf.path.end());
        if (find_deref(state, root->first, root->second, path, nullptr).has_value()) {
            continue;
        }
        Local entry;
        entry.declaration = object->declaration;
        entry.version = next_version++;
        entry.type = leaf.type;
        entry.path = std::move(path);
        entry.pointer = root->first;
        entry.pointer_version = root->second;
        constexpr std::string_view implicit = "this->";
        entry.spelling = pointer + "->" +
                         (leaf.spelling.starts_with(implicit) ? leaf.spelling.substr(implicit.size()) : leaf.spelling);
        state.push_back(std::move(entry));
    }
    return true;
}

bool BodyLowering::materialize_derefs(CXCursor cursor, Locals& state, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return true;
    }
    const auto kind = clang_getCursorKind(cursor);
    // A subscript of a vector, a string or a span forms its element place
    // at the current generation, owing its bound (RFC 0020 §3).
    if (is_sequence_subscript(cursor)) {
        if (!resolve_sequence_element(cursor, state, Capability::Kind::Readable)) {
            return false;
        }
        const std::optional<SequenceCall> subscript = sequence_call(strip_parens(cursor));
        return !subscript || subscript->arguments.empty() ||
               materialize_derefs(subscript->arguments.front(), state, depth + 1);
    }
    // A subscript of a tracked `std::array` is its element place, exactly as
    // a built-in array's is; an untracked one is observed as a value.
    if (kind == CXCursor_CallExpr) {
        if (const std::optional<SequenceCall> call = sequence_call(cursor);
            call && call->family == source::RepresentationKind::StdArray && !call->constructor) {
            library_models.insert(source::RepresentationKind::StdArray);
            if (call->name == "operator[]" && call->arguments.size() == 1) {
                // The array is tracked when the declaration its access is
                // rooted in is: a local's or a parameter's, or for a member of
                // the implicit object, its class. What a pointer designates is
                // a dereference place, formed under a capability, never here.
                const auto access = resolve_access(cursor);
                const bool tracked =
                    access && !access->dereferenced && std::ranges::any_of(state, [&](const Local& entry) {
                        return clang_equalCursors(entry.declaration, access->declaration) != 0;
                    });
                if (tracked && !access->symbolic_indices.empty() && !resolve_symbolic_element(state, *access)) {
                    return false;
                }
                return materialize_derefs(call->arguments.front(), state, depth + 1);
            }
        }
    }
    // A member function called on the object a pointer designates forms
    // that object's places, under the pointer's capability (SPEC.md
    // CLASS-011). Its arguments form theirs as any expression's do.
    if (kind == CXCursor_CallExpr) {
        const std::optional<bool> formed = form_pointee_receiver(cursor, state);
        if (!formed.has_value()) {
            return false;
        }
        if (*formed) {
            for (int index = 0; index < clang_Cursor_getNumArguments(cursor); ++index) {
                if (!materialize_derefs(clang_Cursor_getArgument(cursor, static_cast<unsigned>(index)), state,
                                        depth + 1)) {
                    return false;
                }
            }
            return true;
        }
    }
    // A symbolic subscript of a tracked array forms its own place.
    if (kind == CXCursor_ArraySubscriptExpr) {
        if (const auto access = resolve_access(cursor);
            access && !access->dereferenced && !access->symbolic_indices.empty()) {
            if (!resolve_symbolic_element(state, *access)) {
                return false;
            }
            return materialize_derefs(children_of(cursor)[1], state, depth + 1);
        }
    }
    // `this` is a pointer the body never dereferences as one: `this->x`,
    // `(*this).x` and `this->f()` reach the implicit object, which is the
    // receiver's own tracked storage and owes no capability (SPEC.md
    // CLASS-008).
    const std::vector<CXCursor> operands = children_of(cursor);
    const bool through_this = !operands.empty() && this_record(operands.front()).has_value();
    const bool dereferences =
        !through_this &&
        ((kind == CXCursor_UnaryOperator && clang_getCursorUnaryOperatorKind(cursor) == CXUnaryOperator_Deref) ||
         ((kind == CXCursor_MemberRefExpr || kind == CXCursor_ArraySubscriptExpr) && !operands.empty() &&
          clang_getCanonicalType(clang_getCursorType(strip_parens(operands.front()))).kind == CXType_Pointer));
    if (dereferences) {
        if (const auto access = resolve_access(cursor); access && access->dereferenced) {
            if (!resolve_storage(cursor, state, Capability::Kind::Readable)) {
                if (rejection.empty()) {
                    rejection = "dereferencing a pointer requires a memory capability this implementation "
                                "could not resolve";
                }
                return false;
            }
            return true;
        }
        rejection = "dereferencing this expression requires a pointer whose storage this implementation "
                    "can identify";
        return false;
    }
    for (const auto child : children_of(cursor)) {
        if (!materialize_derefs(child, state, depth + 1)) {
            return false;
        }
    }
    return true;
}

// Form the place of every dereference and subscript `cursor` reads, for the
// statement or condition evaluating it to bind (`bind_formed_derefs`).
bool BodyLowering::form_places(CXCursor cursor, Locals& state) {
    const std::size_t formed = state.size();
    if (!materialize_derefs(cursor, state)) {
        return false;
    }
    for (std::size_t index = formed; index < state.size(); ++index) {
        if (state[index].is_deref() || state[index].symbolic) {
            formed_derefs.push_back(state[index]);
        }
    }
    return true;
}

} // namespace cppl::clangbridge::detail
