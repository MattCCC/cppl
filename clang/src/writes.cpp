#include "access.hpp"
#include "call_objects.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "expressions.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "signature.hpp"
#include "types.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// The one write of tracked storage in a verified body (SPEC.md 12.10, RFC 0014
// §5): the place a write targets, resolved as a read is, the storage a
// reference the callee only reads designates, and assignments and compound
// updates, each charged the place's refinement.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::strip_parens;
using bridge::take;

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

} // namespace cppl::clangbridge::detail
