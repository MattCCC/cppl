#include "aggregate_values.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "types.hpp"

#include <algorithm>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The members an aggregate initializer leaves out (C++ [dcl.init.aggr]): each is
// copy-initialized from an empty initializer list, which for a scalar is its
// zero and for an aggregate is the same rule again, member by member. A member
// with a default member initializer takes that initializer instead, and a class
// with a constructor of its own runs it; neither is modeled, so each is refused
// by name rather than read as a zero it may not hold.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::record_fields;
using bridge::take;

namespace {

// Whether `field` is declared with a default member initializer, `= value` or
// `{value}` after its declarator. The C API reports no such initializer, so it
// is read from the field's own tokens past its name: an array bound written
// there holds neither token, and anything that does is taken for one, which
// only refuses more.
bool has_default_member_initializer(CXCursor field) {
    CXTranslationUnit unit = clang_Cursor_getTranslationUnit(field);
    unsigned name_offset = 0;
    clang_getFileLocation(clang_getCursorLocation(field), nullptr, nullptr, nullptr, &name_offset);
    CXToken* tokens = nullptr;
    unsigned count = 0;
    clang_tokenize(unit, clang_getCursorExtent(field), &tokens, &count);
    bool initialized = false;
    for (unsigned index = 0; index < count && !initialized; ++index) {
        unsigned offset = 0;
        clang_getFileLocation(clang_getTokenLocation(unit, tokens[index]), nullptr, nullptr, nullptr, &offset);
        const std::string spelled = take(clang_getTokenSpelling(unit, tokens[index]));
        initialized = offset > name_offset && (spelled == "=" || spelled == "{");
    }
    clang_disposeTokens(unit, tokens, count);
    return initialized;
}

// Whether an initializer element names the member it initializes, `.m = v` or
// `[i] = v`: the members it leaves out are then not the trailing ones, so the
// elements are not matched to members by position.
bool designated(CXCursor element) {
    CXTranslationUnit unit = clang_Cursor_getTranslationUnit(element);
    CXToken* tokens = nullptr;
    unsigned count = 0;
    clang_tokenize(unit, clang_getCursorExtent(element), &tokens, &count);
    const std::string first = count > 0 ? take(clang_getTokenSpelling(unit, tokens[0])) : std::string();
    clang_disposeTokens(unit, tokens, count);
    return first == "." || first == "[";
}

// Whether the class `record`, canonical, declares a constructor of its own. C++
// then runs a constructor where the class is value-initialized, rather than
// initializing each member from an empty list.
bool declares_constructor(CXType record) {
    const CXCursor definition = clang_getCursorDefinition(clang_getTypeDeclaration(record));
    for (const CXCursor member : children_of(definition)) {
        if (clang_getCursorKind(member) == CXCursor_Constructor ||
            clang_getCursorKind(member) == CXCursor_FunctionTemplate) {
            return true;
        }
    }
    return false;
}

// The type of each member of `aggregate`, canonical, in component order: its
// data members, its elements, or a `std::array`'s elements. Nothing when its
// members are not ones this reads, which refuses.
std::optional<std::vector<CXType>> member_types(CXType aggregate, std::size_t components) {
    const CXType canonical = clang_getCanonicalType(aggregate);
    std::vector<CXType> members;
    if (canonical.kind == CXType_ConstantArray) {
        members.assign(components, clang_getArrayElementType(canonical));
    } else if (canonical.kind == CXType_Record &&
               library_kind(clang_getTypeDeclaration(canonical)) == source::RepresentationKind::StdArray) {
        members.assign(components, clang_Type_getTemplateArgumentAsType(canonical, 0));
    } else if (canonical.kind == CXType_Record &&
               library_kind(clang_getTypeDeclaration(canonical)) == source::RepresentationKind::Record) {
        for (const CXCursor field : record_fields(canonical)) {
            members.push_back(clang_getCursorType(field));
        }
    }
    if (members.size() != components ||
        std::ranges::any_of(members, [](CXType member) { return member.kind == CXType_Invalid; })) {
        return std::nullopt;
    }
    return members;
}

} // namespace

// Each member past the last element of `list` is value-initialized. A leaf is
// recorded with no initializer and its zero as its first value; a member that
// is itself an aggregate is followed into its members, all value-initialized.
// The rule is refused where C++ does something else: a designated element, a
// scalar member given a braced list, a default member initializer, a class with
// a constructor of its own, a bit-field, or a member of a type not tracked.
std::optional<std::string> BodyLowering::value_initialized(CXCursor list, const Type& type, const std::string& written,
                                                           const std::vector<PlaceStep>& prefix,
                                                           std::vector<AggregateLeaf>& leaves) {
    const std::vector<CXCursor> elements = children_of(list);
    const std::size_t components = type.representation.components.size();
    if (elements.size() >= components) {
        return std::nullopt;
    }
    const std::string refused = "'" + written + "' of type '" + type.spelling + "' leaves out ";
    for (std::size_t member = 0; member < elements.size(); ++member) {
        if (designated(elements[member])) {
            return refused + "members with a designated initializer, which is not modeled";
        }
        const TypeKind kind = type.projections[member].kind;
        if ((kind == TypeKind::Int || kind == TypeKind::Bool) &&
            clang_getCursorKind(elements[member]) == CXCursor_InitListExpr) {
            return refused + "members, and gives a scalar member a braced list, which is not modeled there";
        }
    }
    // What is left out of a member that is itself an aggregate is followed into
    // its members, which are all left out.
    struct Pending {
        CXType type;
        const Type* model;
        std::vector<PlaceStep> path;
        std::string spelling;
        std::optional<CXCursor> field;
    };
    std::vector<Pending> pending;
    const auto enqueue = [&](CXType aggregate, const Type& model, const std::vector<PlaceStep>& at,
                             const std::string& spelled, std::size_t from) -> std::optional<std::string> {
        if (at.size() >= kMaxPlaceDepth) {
            return "'" + spelled + "' nests deeper than this implementation tracks";
        }
        const auto members = member_types(aggregate, model.representation.components.size());
        if (!members.has_value() || !aggregates::structural(model)) {
            return refused + "'" + spelled + "', whose members are not ones this implementation value-initializes";
        }
        const CXType canonical = clang_getCanonicalType(aggregate);
        const bool record = model.representation.kind == source::RepresentationKind::Record;
        if (record && declares_constructor(canonical)) {
            return refused + "a member of '" + spelled + "', whose class '" + model.spelling +
                   "' declares a constructor, which value-initialization would run";
        }
        const std::vector<CXCursor> fields = record ? record_fields(canonical) : std::vector<CXCursor>{};
        for (std::size_t index = model.representation.components.size(); index > from; --index) {
            const std::size_t at_member = index - 1;
            std::vector<PlaceStep> path = at;
            path.push_back(PlaceStep{record ? PlaceStep::Kind::Field : PlaceStep::Kind::Element,
                                     static_cast<std::uint32_t>(at_member)});
            std::string member_spelling = spelled;
            member_spelling += record ? "." : "[";
            member_spelling += model.representation.components[at_member].name;
            member_spelling += record ? "" : "]";
            pending.push_back(Pending{(*members)[at_member], &model.projections[at_member], std::move(path),
                                      std::move(member_spelling),
                                      record ? std::optional<CXCursor>{fields[at_member]} : std::nullopt});
        }
        return std::nullopt;
    };
    if (auto refusal = enqueue(clang_getCursorType(list), type, prefix, written, elements.size())) {
        return refusal;
    }
    // Taken last-in first-out, so the leaves are recorded in declaration order.
    while (!pending.empty()) {
        Pending next = std::move(pending.back());
        pending.pop_back();
        if (next.field.has_value() &&
            (clang_Cursor_isBitField(*next.field) != 0 || has_default_member_initializer(*next.field))) {
            return refused + "'" + next.spelling +
                   "', which a default member initializer or a bit-field width initializes, which is not modeled";
        }
        if (leaves.size() >= kMaxTrackedLeaves) {
            return "'" + written + "' has more tracked members than the proof resource limit allows";
        }
        if (next.model->kind == TypeKind::Int || next.model->kind == TypeKind::Bool) {
            leaves.push_back(AggregateLeaf{next.path, *next.model, clang_getNullCursor(), next.spelling, true});
            continue;
        }
        if (next.model->kind != TypeKind::Value) {
            return refused + "'" + next.spelling + "' of type '" + next.model->spelling +
                   "', whose value-initialization is not modeled";
        }
        if (auto refusal = enqueue(next.type, *next.model, next.path, next.spelling, 0)) {
            return refusal;
        }
    }
    return std::nullopt;
}

// A value-initialized leaf starts at the zero of its type, `false` for `bool`;
// any other leaf at the value its initializer element evaluates to. The leaf is
// bound at its declared type, so a refined member left out owes its predicate
// of that zero as one given a value does.
std::optional<Expr> BodyLowering::initial_value(const AggregateLeaf& leaf, Locals& state,
                                                std::vector<std::size_t>& invalidated, CXCursor at) {
    if (!leaf.value_initialized) {
        return evaluate(leaf.initializer, state, invalidated);
    }
    Expr zero;
    zero.type = leaf.type;
    zero.type.refinements.clear(); // a value is not the storage's type
    zero.location = presumed_location(clang_getCursorLocation(at));
    zero.node = IntLiteral{0};
    return zero;
}

} // namespace cppl::clangbridge::detail
