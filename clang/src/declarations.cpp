#include "access.hpp"
#include "aggregate_values.hpp"
#include "call_objects.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/representation.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "refinements.hpp"
#include "types.hpp"

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

// Local declarations in a verified body (SPEC.md 12.8, 12.9, 12.10): a local's
// first version, a reference bound to the place its initializer names, and an
// aggregate local tracked as one place per scalar member, each owing its
// declared refinement where it is initialized.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::take;

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
    if (elements.size() > components.size()) {
        return "'" + written + "' of type '" + type.spelling + "' is initialized with " +
               std::to_string(elements.size()) + " values for " + std::to_string(components.size()) +
               " members; an initializer that leaves out a nested member's braces is not modeled";
    }
    for (std::size_t member = 0; member < elements.size(); ++member) {
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
    return value_initialized(list, type, written, prefix, leaves);
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
        auto evaluated = initial_value(leaf, declaring, invalidated, declaration);
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

} // namespace cppl::clangbridge::detail
