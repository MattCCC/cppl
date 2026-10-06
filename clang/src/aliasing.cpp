#include "access.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/storage.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "sequences.hpp"
#include "signature.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Which places a write may reach (SPEC.md 12.10, RFC 0014 §4, RFC 0020 §3, §4):
// the common alias model, a container's new storage generation, the versions a
// write or an unidentified pointee leaves to whatever may alias it, and the
// container writes a loop carries. Disjointness is concluded only from what
// Clang resolves.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::strip_parens;

// Mark every entry a container operation in `root` may write, for a loop
// that carries them (RFC 0020 §4, SPEC.md 24.2). A mutator writes the
// container's root and whatever may alias it; an element write reaches
// what the element place may alias; a container passed by mutable
// reference reaches both; a move writes the container moved from. Which
// entries those are is the alias analysis's answer, the same one the
// lowering gives, so a loop carries exactly what an iteration may change.
void BodyLowering::mark_sequence_writes(CXCursor root, const Locals& locals, std::vector<bool>& written,
                                        unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return;
    }
    const auto reach = [&](const Local& target) {
        for (std::size_t index = 0; index < locals.size(); ++index) {
            if (!locals[index].referent.has_value() && may_alias(target, locals[index])) {
                written[index] = true;
            }
        }
    };
    const auto container = [&](CXCursor object) {
        if (const auto found = owning_root(object, locals)) {
            written[*found] = true;
            reach(locals[*found]);
        }
    };
    const CXCursorKind kind = clang_getCursorKind(root);
    if (const std::optional<SequenceCall> call = sequence_call(root)) {
        if (!call->constructor && is_mutator(*call)) {
            container(call->object);
            if (call->name == "operator=" && call->arguments.size() == 1) {
                if (const auto moved = moved_operand_of(call->arguments.front())) {
                    container(*moved);
                }
            }
        }
        if (call->constructor && call->arguments.size() == 1) {
            if (const auto moved = moved_operand_of(call->arguments.front())) {
                container(*moved);
            }
        }
    } else if (kind == CXCursor_CallExpr) {
        const std::vector<CXCursor> formals = parameters_of(clang_getCursorReferenced(root));
        for (std::size_t index = 0; index < formals.size(); ++index) {
            if (source::may_write(passing_of(clang_getCursorType(formals[index])))) {
                container(clang_Cursor_getArgument(root, static_cast<unsigned>(index)));
            }
        }
    }
    const bool writes =
        (kind == CXCursor_BinaryOperator && clang_getCursorBinaryOperatorKind(root) == CXBinaryOperator_Assign) ||
        kind == CXCursor_CompoundAssignOperator ||
        (kind == CXCursor_UnaryOperator && (clang_getCursorUnaryOperatorKind(root) == CXUnaryOperator_PreInc ||
                                            clang_getCursorUnaryOperatorKind(root) == CXUnaryOperator_PostInc ||
                                            clang_getCursorUnaryOperatorKind(root) == CXUnaryOperator_PreDec ||
                                            clang_getCursorUnaryOperatorKind(root) == CXUnaryOperator_PostDec));
    if (writes) {
        const std::vector<CXCursor> operands = children_of(root);
        const std::optional<SequenceCall> subscript = !operands.empty() && is_sequence_subscript(operands.front())
                                                          ? sequence_call(strip_parens(operands.front()))
                                                          : std::nullopt;
        if (subscript.has_value()) {
            if (auto region = element_region(subscript->object, locals, signature)) {
                Local element;
                element.declaration = region->declaration;
                element.path = {PlaceStep{PlaceStep::Kind::SymbolicElement, 0, 0}};
                element.external = region->external;
                element.symbolic = true;
                element.type = region->element;
                reach(element);
            }
        }
    }
    for (const CXCursor child : children_of(root)) {
        mark_sequence_writes(child, locals, written, depth + 1);
    }
}

// Whether a write to `target` may reach `other`, so facts about `other`
// cannot survive it (SPEC.md 12.10, RFC 0014 §4).
//
// Disjointness is proved, never assumed, and only from what Clang
// resolves. Two places rooted in distinct locals are disjoint because no
// two locals share storage. Within one object, paths that differ at some
// step are disjoint because they select different members. A write to an
// object reaches the members inside it, and a write to a member reaches
// the object it belongs to, because they are the same storage seen at
// different granularity.
//
// Everything else may alias. Two by-reference parameters may designate one
// object, so a write through either invalidates the other. No type-based
// argument is used: strict aliasing is valid C++ inference, but it
// presupposes the undefined-behavior freedom a proof has not established,
// so using it here would make the proof circular (AGENTS.md storage
// invariants).
// A dereference designates storage this body cannot name, so it is the
// conservative case: two dereferences may always alias, and a dereference
// may alias any storage whose address could have reached a pointer. Only
// the address-taken locals are at risk, because a local whose address is
// never taken cannot be the pointee of any pointer -- and that is a fact
// Clang resolves, not a type-based argument (RFC 0014 §4).
bool BodyLowering::may_alias(const Local& target, const Local& other) const {
    // A container's modeled value is its length, and its object's storage
    // is disjoint from every live scalar object: a scalar holds no other
    // object, and a container's elements live in storage the container
    // allocated rather than in the container object (C++ [intro.object]).
    // So a write to a scalar place -- an element, a local, a reference's
    // referent -- never reaches a container's root (RFC 0020 §3). A write
    // through a pointer is not such a write: the pointer's declared pointee
    // type is no evidence about the object it designates.
    if (other.sequence.has_value() && !target.sequence.has_value() && !target.is_deref()) {
        return false;
    }
    if (target.is_deref() || other.is_deref()) {
        if (target.is_deref() && other.is_deref()) {
            // Same pointer and same pointer version: one place, so the
            // path decides, and a symbolic index may select any element,
            // as for an array (RFC 0014 §4). Otherwise two unrelated
            // pointees, which may overlap for all this implementation can
            // prove.
            if (target.pointer == other.pointer && target.pointer_version == other.pointer_version) {
                if (other.has_symbolic_step() || target.has_symbolic_step()) {
                    return true;
                }
                return target.covered_by(other) || other.covered_by(target);
            }
            return true;
        }
        const Local& storage = target.is_deref() ? other : target;
        return storage.external || escaped.contains(clang_hashCursor(storage.declaration));
    }
    if (clang_equalCursors(target.declaration, other.declaration) != 0) {
        // Distinct members of one object are distinct storage, by Clang's
        // resolved member identity, wherever else their paths select an
        // element at a term.
        if (target.diverges_from(other)) {
            return false;
        }
        // A symbolic index selects an element this implementation cannot
        // decide, so two element places of one array may be the same
        // element unless their indices are proved unequal. That proof does
        // not exist here, so they are assumed to overlap: a false rejection
        // is preferable to a stale fact (RFC 0014 §4, AGENTS.md storage
        // invariants).
        if (target.has_symbolic_step() || other.has_symbolic_step()) {
            return true;
        }
        return target.covered_by(other) || other.covered_by(target);
    }
    // Distinct locals never share storage. A by-reference parameter
    // designates caller storage, which any other such parameter may
    // designate too.
    return target.external && other.external;
}

// Gives a container's root a new storage generation, recording what
// established it for the diagnostic of a view used after it, and returns
// the call effect that writes the root in the call's first position
// (STDMODEL-015).
CallEffect BodyLowering::new_generation(Local& root, std::string reason) {
    root.version = next_version++;
    if (root.sequence.has_value()) {
        root.sequence->invalidated = std::move(reason);
    }
    return CallEffect{0, root.version, root.type};
}

std::vector<std::size_t> BodyLowering::invalidate_aliases(std::size_t storage, Locals& state) {
    std::vector<std::size_t> changed;
    const Local target = state[storage];
    for (std::size_t index = 0; index < state.size(); ++index) {
        if (index == storage || state[index].referent.has_value()) {
            continue;
        }
        if (Local& reached = state[index]; may_alias(target, reached)) {
            reached.version = next_version++;
            // A container reached this way has a new storage generation
            // too, and a view of it formed before is stale (STDMODEL-015).
            if (reached.sequence.has_value()) {
                reached.sequence->invalidated =
                    "a write to '" + target.spelling + "', which may designate the same container";
            }
            changed.push_back(index);
        }
    }
    return changed;
}

// Every place that may alias the pointee of a pointer nothing identifies,
// other than those in `handed` or `invalidated`. Such a pointee may be any
// place `may_alias` does not keep apart from a dereference, so the stand-in
// names no pointer entry a real place carries, and is kept apart from none.
std::vector<std::size_t> BodyLowering::invalidate_pointee_aliases(Locals& state, const std::vector<std::size_t>& handed,
                                                                  const std::vector<std::size_t>& invalidated) {
    Local pointee;
    pointee.pointer = std::numeric_limits<std::size_t>::max();
    pointee.spelling = "a pointee a callee may write";
    std::vector<std::size_t> changed;
    for (std::size_t index = 0; index < state.size(); ++index) {
        if (state[index].referent.has_value() || std::ranges::find(handed, index) != handed.end() ||
            std::ranges::find(invalidated, index) != invalidated.end()) {
            continue;
        }
        if (Local& reached = state[index]; may_alias(pointee, reached)) {
            reached.version = next_version++;
            if (reached.sequence.has_value()) {
                reached.sequence->invalidated = "a call that may write through a pointer designating it";
            }
            changed.push_back(index);
        }
    }
    return changed;
}

} // namespace cppl::clangbridge::detail
