#include "cppl/clang/bridge.hpp"

#include "access.hpp"
#include "call_objects.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/projection.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
#include "expressions.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "proof_instantiation.hpp"
#include "refinements.hpp"
#include "signature.hpp"
#include "types.hpp"
#include "unsafe.hpp"

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
#include <optional>
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
