#pragma once

#include "aggregate_values.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/source/storage.hpp"
#include "places.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

// What a verified body or a clause is lowered against: the parameters Clang
// resolved for it and, for a non-static member function, its implicit object.
namespace cppl::clangbridge::detail {

// One scalar place of a member function's implicit object: a data member of the
// class, or a member or an element of one, reached from the object by `path`
// (SPEC.md CLASS-008). The path numbers members the way `field_index_of` does,
// so `this->a.b`, `(*this).a.b` and `a.b` in the member function all resolve to
// exactly this leaf.
struct ReceiverLeaf {
    std::vector<PlaceStep> path;
    Type type; // with the refinement the member declared (SPEC.md 17.6)
    std::string spelling;
    // Reached through a `mutable` member, which a `const` member function may
    // still write (SPEC.md CONTRACT-010, CLASS-009).
    bool mutable_member = false;
};

// The implicit object of a non-static member function, as the verified callable
// sees it (SPEC.md CLASS-008): the receiver place, rooted in the class the
// function belongs to, and the scalar places projected from it.
//
// This implementation lowers the receiver to one reference parameter per
// scalar place, standing before the written parameters. The lowering is proof
// bookkeeping, never a runtime parameter (CLASS-013), and it keeps the object's
// identity and alias relations: every place is projected from one root by the
// numbering a member access in the body resolves to, a caller passes the places
// of one object as one object, and the places are external storage the common
// alias model relates to every other access path (`BodyLowering::may_alias`).
//
// A member whose type this implementation does not model -- a pointer, a
// reference, a floating-point value, a library type, a volatile object, a class
// with a base, a union -- has no place. It is storage a verified body can
// neither read nor write, so nothing is known or claimed about it, and a body
// that names it is refused where it does.
struct Receiver {
    CXCursor record = clang_getNullCursor(); // canonical class declaration
    std::vector<ReceiverLeaf> leaves;
    bool constant = false; // a `const` member function

    // How the member function observes each place (SPEC.md CLASS-009): a
    // `const` one reads its object and may write only a `mutable` member of it,
    // and any other may write every place. A ref-qualifier is not consulted: it
    // decides which receivers C++ lets the call be made on, which Clang's
    // overload resolution has already settled, and inside the body a member of
    // an `&&` member function's object is storage like any other.
    [[nodiscard]] source::ParameterPassing passing(const ReceiverLeaf& leaf) const {
        if (constant && !leaf.mutable_member) {
            return source::ParameterPassing::ConstReference;
        }
        return source::ParameterPassing::MutableReference;
    }

    // The position of the leaf at `path`, which is its parameter position.
    [[nodiscard]] std::optional<std::size_t> leaf_at(const std::vector<PlaceStep>& path) const {
        for (std::size_t index = 0; index < leaves.size(); ++index) {
            if (leaves[index].path == path) {
                return index;
            }
        }
        return std::nullopt;
    }

    // Whether the member function may write any leaf.
    [[nodiscard]] bool writes() const {
        return std::ranges::any_of(leaves,
                                   [this](const ReceiverLeaf& leaf) { return source::may_write(passing(leaf)); });
    }
};

class UnsafeEffects;

struct ChosenArm;

// What a body or a clause is lowered against: the parameters Clang resolved for
// its declaration and, for a non-static member function, its implicit object.
// The verified callable takes the implicit object's leaves first and the
// written parameters after them, so a written parameter stands at its own
// position moved past the leaves (SPEC.md CLASS-008).
struct Signature {
    std::vector<CXCursor> parameters;
    std::optional<Receiver> receiver;
    // A clause states no body in which a leaf is written, so it reads each leaf
    // as the parameter it is. A body tracks every leaf as storage instead, and
    // reads it at the version current where the read stands.
    bool clause = false;
    // The unit's refinements, so a type a proposition writes for itself, a
    // quantifier's binder or an equality's operand, keeps the refinement Clang
    // canonicalizes away (SPEC.md FORALL-001). A reference, so no signature can
    // be made without them.
    const std::vector<Selection::Refinement>& refinements;
    // Which callees may write through a `const` access path they are handed,
    // through an unsafe block (TRUST.md TCB-UNSAFE-004). Set for a body that
    // runs, where such a call is followed only as a statement of its own; a
    // clause runs nothing.
    UnsafeEffects* unsafe_effects = nullptr;
    // The arms the route being lowered evaluates in place of the selections
    // that hold them (statements.hpp `ChosenArm`): the body lowering's own,
    // so a selection standing anywhere in a statement's value is read as the
    // arm its route takes. None for a clause, which takes no route.
    const std::vector<ChosenArm>* chosen = nullptr;

    [[nodiscard]] std::uint32_t leaves() const {
        return receiver.has_value() ? static_cast<std::uint32_t>(receiver->leaves.size()) : 0;
    }

    // The callable position of written parameter `index`.
    [[nodiscard]] std::uint32_t position(std::size_t index) const {
        return leaves() + static_cast<std::uint32_t>(index);
    }

    // The written parameter `declaration` names, by its callable position.
    [[nodiscard]] std::optional<std::uint32_t> position_of(CXCursor declaration) const {
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (clang_equalCursors(parameters[index], declaration) != 0) {
                return position(index);
            }
        }
        return std::nullopt;
    }
};

// A member function's standing as a verified callable: its implicit object, or
// why this implementation does not verify it (SPEC.md CLASS-008, CLASS-014,
// CLASS-015). A function that is not a member, and a static member, has no
// implicit object and nothing to refuse here.
struct MemberStanding {
    std::optional<Receiver> receiver;
    std::optional<std::string> rejection;
};

std::expected<Receiver, std::string> receiver_of(CXCursor method, const std::vector<Selection::Refinement>* known);

aggregates::Frame frame_of(const Signature& signature);

bool on_implicit_object(CXCursor cursor);

std::optional<std::string> unmodeled_member(CXCursor cursor);

Expr read_receiver(const ResolvedAccess& access, CXCursor cursor, const Signature& signature, const Locals& locals);

std::vector<CXCursor> parameters_of(CXCursor cursor);

MemberStanding member_standing(CXCursor cursor, const std::vector<Selection::Refinement>& known);

} // namespace cppl::clangbridge::detail
