#include "signature.hpp"

#include "access.hpp"
#include "aggregate_values.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "places.hpp"
#include "refinements.hpp"
#include "types.hpp"

#include <cctype>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// What a verified body or a clause is lowered against (SPEC.md CLASS-008): the
// parameters Clang resolved, the implicit object of a member function and its
// scalar places, a read of that object's storage, and a member function's
// standing as a verified callable.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::record_fields;
using bridge::record_has_base;
using bridge::strip_parens;
using bridge::take;

namespace {

// The scalar leaves of the storage `path` names, of written type `written`,
// appended to `leaves` in declaration order (SPEC.md CLASS-008). A member that
// is itself a record or an array is followed into its own members and elements,
// so `this->a.b` and `this->items[2].v` are places exactly as `this->a` is. A
// member this implementation does not model is left without a leaf, and so is
// storage nested past the depth places are tracked to. `field` is the data
// member the storage belongs to, whose declaration names any refinement.
std::optional<std::string> collect_receiver_leaves(CXType written, CXCursor field, const std::vector<PlaceStep>& path,
                                                   const std::string& spelling, bool mutable_member,
                                                   const std::vector<Selection::Refinement>* known,
                                                   std::vector<ReceiverLeaf>& leaves) {
    if (path.size() > kMaxPlaceDepth) {
        return std::nullopt;
    }
    // A volatile object may change by means no program statement shows, so no
    // version of it could be followed (SPEC.md CLASS-009, CLASS-015).
    if (clang_isVolatileQualifiedType(written) != 0) {
        return std::nullopt;
    }
    const CXType canonical = clang_getCanonicalType(written);
    // Scalar places are pairwise disjoint: two objects that are not bit-fields
    // share storage only when one is nested in the other or one has no size
    // (C++ [intro.object]), and a scalar has a size and nests nothing. That holds
    // with `[[no_unique_address]]` too, which lets an empty member share an
    // address, never a scalar's storage. The forms that do overlap are not
    // followed into: a union, whose members share storage, a bit-field, which
    // shares a memory location with its neighbours, and a reference member,
    // which designates storage outside the object that may be any place,
    // another member of this one included (SPEC.md CLASS-010).
    if (canonical.kind == CXType_Record) {
        const CXCursor declaration = clang_getTypeDeclaration(canonical);
        const CXCursor definition = clang_getCursorDefinition(declaration);
        if (clang_Cursor_isNull(definition) != 0 || clang_getCursorKind(definition) == CXCursor_UnionDecl ||
            record_has_base(canonical) || library_kind(declaration) != source::RepresentationKind::Record) {
            return std::nullopt;
        }
        const auto fields = record_fields(canonical);
        for (std::size_t index = 0; index < fields.size(); ++index) {
            // A bit-field has no place of its own (`field_index_of`).
            if (clang_getFieldDeclBitWidth(fields[index]) >= 0) {
                continue;
            }
            std::vector<PlaceStep> member = path;
            member.push_back(PlaceStep{PlaceStep::Kind::Field, static_cast<std::uint32_t>(index)});
            const std::string name =
                spelling + (path.empty() ? "" : ".") + take(clang_getCursorSpelling(fields[index]));
            if (auto refused = collect_receiver_leaves(clang_getCursorType(fields[index]), fields[index], member, name,
                                                       mutable_member || clang_CXXField_isMutable(fields[index]) != 0,
                                                       known, leaves)) {
                return refused;
            }
        }
        return std::nullopt;
    }
    if (canonical.kind == CXType_ConstantArray) {
        const long long count = clang_getArraySize(canonical);
        if (count < 0 || count > static_cast<long long>(kMaxTrackedLeaves)) {
            return std::nullopt;
        }
        // The element type is taken from the written array type, so a
        // refinement the element type names is kept (SPEC.md 17.3).
        CXType element = clang_getArrayElementType(written);
        if (element.kind == CXType_Invalid) {
            element = clang_getArrayElementType(canonical);
        }
        for (long long position = 0; position < count; ++position) {
            std::vector<PlaceStep> at = path;
            at.push_back(PlaceStep{PlaceStep::Kind::Element, static_cast<std::uint32_t>(position)});
            if (auto refused =
                    collect_receiver_leaves(element, field, at, spelling + "[" + std::to_string(position) + "]",
                                            mutable_member, known, leaves)) {
                return refused;
            }
        }
        return std::nullopt;
    }
    Type converted = convert_type(written, 0, ReferenceModel::Opaque, known);
    if (path.empty() || (converted.kind != TypeKind::Int && converted.kind != TypeKind::Bool)) {
        return std::nullopt;
    }
    if (known != nullptr) {
        auto declared = refinements_of(field, written, *known);
        if (!declared) {
            return "member '" + spelling + "' has " + declared.error().message;
        }
        converted.refinements = std::move(*declared);
    }
    if (leaves.size() >= kMaxTrackedLeaves) {
        return "the object has more members than the proof resource limit allows";
    }
    leaves.push_back(ReceiverLeaf{path, std::move(converted), spelling, mutable_member});
    return std::nullopt;
}

// Why an access rooted in the implicit object names no leaf of the receiver
// `signature` has, or nothing when it names one.
std::optional<std::string> receiver_gap(const ResolvedAccess& access, const Signature& signature) {
    if (std::optional<std::string> unmodeled = unmodeled_member(access.object); unmodeled.has_value()) {
        return unmodeled;
    }
    if (!signature.receiver.has_value()) {
        return "'this' names no object here: only a non-static member function has an implicit object";
    }
    if (clang_equalCursors(access.declaration, signature.receiver->record) == 0) {
        return "this member belongs to a class other than the one this member function is declared in, and a base "
               "subobject is not modeled";
    }
    if (!signature.receiver->leaf_at(access.path).has_value()) {
        return "this member of the implicit object is not tracked storage: its type is not one this implementation "
               "models";
    }
    return std::nullopt;
}

// Whether member function `method` is volatile-qualified, `f() volatile`. The C
// API reports `const` directly but not `volatile`, so it is read from the
// function type Clang prints, `R (P...) const volatile &&`: a qualifier stands
// after the parameter list closes, outside every parenthesis.
bool volatile_member_function(CXCursor method) {
    const std::string spelling = take(clang_getTypeSpelling(clang_getCursorType(method)));
    int depth = 0;
    bool parameters_closed = false;
    std::string word;
    for (const char character : spelling + " ") {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_') {
            word.push_back(character);
            continue;
        }
        if (parameters_closed && depth == 0 && word == "volatile") {
            return true;
        }
        word.clear();
        if (character == '(') {
            ++depth;
        } else if (character == ')') {
            --depth;
            parameters_closed = parameters_closed || depth == 0;
        }
    }
    return false;
}

} // namespace

// The implicit object of `method`, or why its object is not one this
// implementation models (SPEC.md CLASS-008, CLASS-015). A static member
// function has none, and is not asked.
std::expected<Receiver, std::string> receiver_of(CXCursor method, const std::vector<Selection::Refinement>* known) {
    Receiver receiver;
    receiver.record = enclosing_record(method);
    const CXCursorKind kind = clang_getCursorKind(receiver.record);
    if (kind == CXCursor_UnionDecl) {
        return std::unexpected("its class is a union, whose active member is not modeled");
    }
    if (kind != CXCursor_StructDecl && kind != CXCursor_ClassDecl) {
        return std::unexpected("it is not a member of a class this implementation models");
    }
    const CXType type = clang_getCanonicalType(clang_getCursorType(receiver.record));
    if (clang_Cursor_isNull(clang_getCursorDefinition(receiver.record)) != 0) {
        return std::unexpected("its class is incomplete");
    }
    if (record_has_base(type)) {
        return std::unexpected("its class has a base subobject, whose storage and dispatch the receiver model does not "
                               "follow");
    }
    receiver.constant = clang_CXXMethod_isConst(method) != 0;
    if (auto refused =
            collect_receiver_leaves(type, clang_getNullCursor(), {}, "this->", false, known, receiver.leaves)) {
        return std::unexpected(*refused);
    }
    return receiver;
}

// What the lowering of whole struct values reads of a signature: where each
// written parameter stands, and the class of the implicit object.
aggregates::Frame frame_of(const Signature& signature) {
    return aggregates::Frame{&signature.parameters, signature.leaves(),
                             signature.receiver.has_value() ? signature.receiver->record : clang_getNullCursor()};
}

// Whether the member access `cursor` is made on the implicit object: `m`,
// `this->m`, or a member of an anonymous struct or union of it, which C++
// names as though it were the object's own.
bool on_implicit_object(CXCursor cursor) {
    cursor = strip_parens(cursor);
    for (unsigned depth = 0; depth < kMaxExpressionDepth; ++depth) {
        if (clang_getCursorKind(cursor) != CXCursor_MemberRefExpr) {
            return false;
        }
        const std::vector<CXCursor> children = children_of(cursor);
        if (children.empty()) {
            return true;
        }
        if (children.size() != 1) {
            return false;
        }
        if (this_record(children.front()).has_value()) {
            return true;
        }
        cursor = strip_parens(children.front());
        const CXCursor field = clang_getCursorReferenced(cursor);
        if (clang_Cursor_isAnonymousRecordDecl(clang_getTypeDeclaration(clang_getCursorType(field))) == 0) {
            return false;
        }
    }
    return false;
}

// Why the data member a member access names has no place of the receiver,
// when its declaration decides it (SPEC.md CLASS-010, CLASS-015). Each is a
// form whose storage may overlap another place or change unseen, so treating
// it as a place of its own would keep facts a write elsewhere took away.
std::optional<std::string> unmodeled_member(CXCursor cursor) {
    const CXCursor field = clang_getCursorReferenced(strip_parens(cursor));
    if (clang_getCursorKind(field) != CXCursor_FieldDecl) {
        return std::nullopt;
    }
    const std::string name = "'" + take(clang_getCursorSpelling(field)) + "'";
    const CXType type = clang_getCursorType(field);
    const CXTypeKind canonical = clang_getCanonicalType(type).kind;
    if (canonical == CXType_LValueReference || canonical == CXType_RValueReference) {
        return name + " is a reference member, which designates storage outside the object that may be any place, "
                      "another member of this object included";
    }
    if (clang_Cursor_isBitField(field) != 0) {
        return name + " is a bit-field, which shares a memory location with the bit-fields beside it";
    }
    if (clang_isVolatileQualifiedType(type) != 0) {
        return name + " is volatile, so its value may change by means no statement of the program shows";
    }
    const CXCursor parent = clang_getCursorSemanticParent(field);
    if (clang_getCursorKind(parent) == CXCursor_UnionDecl) {
        return name + " is a member of a union, whose members share one storage";
    }
    if (clang_Cursor_isAnonymousRecordDecl(parent) != 0) {
        return name + " is a member of an anonymous struct, whose members this implementation does not follow";
    }
    return std::nullopt;
}

// A read of the implicit object's storage (SPEC.md CLASS-008).
//
// A body tracks every leaf of its receiver as storage from entry, so it reads
// one at the version current where the read stands, through the one read path.
// A clause states no body: in it, a leaf is the parameter it stands for, at the
// state the clause describes -- entry for a precondition, the normal-return
// post-state for a postcondition (SPEC.md CONTRACT-009).
Expr read_receiver(const ResolvedAccess& access, CXCursor cursor, const Signature& signature, const Locals& locals) {
    if (!signature.clause) {
        if (const auto entry = tracked_place(cursor, locals, signature)) {
            return read_place(locals, *entry, cursor);
        }
        // A member of the implicit object that is itself a record or an array is
        // read whole as its leaves assemble it (TRUST.md TCB-AGGREGATE-001).
        if (std::optional<Expr> whole = aggregates::receiver_value(cursor, access, locals, frame_of(signature))) {
            return std::move(*whole);
        }
        return unsupported_expression(cursor, receiver_gap(access, signature)
                                                  .value_or("this member of the implicit object is not tracked "
                                                            "storage of this body"));
    }
    if (!access.symbolic_indices.empty()) {
        return unsupported_expression(cursor, "a contract does not select an element of a member array at a term; "
                                              "the implicit object's elements are parameters of their own");
    }
    if (const auto gap = receiver_gap(access, signature)) {
        return unsupported_expression(cursor, *gap);
    }
    const std::optional<std::size_t> leaf =
        signature.receiver.has_value() ? signature.receiver->leaf_at(access.path) : std::nullopt;
    if (!leaf.has_value()) {
        return unsupported_expression(cursor, "this member of the implicit object is not tracked storage");
    }
    Expr expr;
    expr.type = convert_type(clang_getCursorType(cursor));
    expr.location = presumed_location(clang_getCursorLocation(cursor));
    expr.node = ParameterRef{static_cast<std::uint32_t>(*leaf), signature.receiver->leaves[*leaf].spelling};
    return expr;
}

std::vector<CXCursor> parameters_of(CXCursor cursor) {
    std::vector<CXCursor> parameters;
    // Negative for a cursor that is not a function; there are then no arguments.
    const int count = clang_Cursor_getNumArguments(cursor);
    if (count <= 0) {
        return parameters;
    }
    parameters.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        parameters.push_back(clang_Cursor_getArgument(cursor, static_cast<unsigned>(index)));
    }
    return parameters;
}

MemberStanding member_standing(CXCursor cursor, const std::vector<Selection::Refinement>& known) {
    MemberStanding standing;
    if (clang_getCursorKind(cursor) != CXCursor_CXXMethod || clang_CXXMethod_isStatic(cursor) != 0) {
        return standing;
    }
    // Checked on the declaration rather than read off its spelling: a function
    // overriding a virtual one is virtual whether or not it says so.
    if (clang_CXXMethod_isVirtual(cursor) != 0) {
        standing.rejection = "it is virtual: a call through its base interface runs whichever override the dynamic "
                             "type selects, and override substitutability is not checked by this implementation "
                             "(SPEC.md CONTRACT-014, CLASS-006, CLASS-014)";
        return standing;
    }
    if (volatile_member_function(cursor)) {
        standing.rejection = "it is volatile-qualified: its object is volatile, whose value may change by means no "
                             "statement shows, and volatile object semantics are not modeled (SPEC.md CLASS-009, "
                             "CLASS-015)";
        return standing;
    }
    const CXCursorKind parent = clang_getCursorKind(clang_getCursorSemanticParent(cursor));
    if (parent == CXCursor_ClassTemplate || parent == CXCursor_ClassTemplatePartialSpecialization) {
        standing.rejection = "it is a member of a class template, whose contract would have to be checked at every "
                             "specialization of the class, which nothing forces here (SPEC.md CLASS-015)";
        return standing;
    }
    const CXCursorKind lexical = clang_getCursorKind(clang_getCursorLexicalParent(cursor));
    if (lexical != CXCursor_StructDecl && lexical != CXCursor_ClassDecl && lexical != CXCursor_UnionDecl) {
        standing.rejection = "its contract is stated on a definition outside its class; a member function's contract "
                             "is stated on its declaration in the class, and the definition inherits it (SPEC.md "
                             "CONTRACT-005, CLASS-008)";
        return standing;
    }
    auto receiver = receiver_of(cursor, &known);
    if (!receiver) {
        standing.rejection =
            "its implicit object is not one this implementation models: " + receiver.error() + " (SPEC.md CLASS-015)";
        return standing;
    }
    standing.receiver = std::move(*receiver);
    return standing;
}

} // namespace cppl::clangbridge::detail
