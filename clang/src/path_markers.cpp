#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "expressions.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "types.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// The blocks the projector emits on a verified body's path (SPEC.md
// VERIFIED-023, CASE-017): a claim that the path cannot occur, which ends it,
// and a case split, whose arms each continue it.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::take;

// The name of the block the projector emitted for a claim that this path
// cannot occur, if `statement` is the declaration that opens one
// (SPEC.md VERIFIED-023).
std::optional<std::string> BodyLowering::contradiction_marker(CXCursor statement) const {
    if (invariant_prefix.empty() || clang_getCursorKind(statement) != CXCursor_DeclStmt) {
        return std::nullopt;
    }
    const std::vector<CXCursor> declared = children_of(statement);
    if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
        return std::nullopt;
    }
    std::string name = take(clang_getCursorSpelling(declared[0]));
    if (!name.starts_with(invariant_prefix + "contradiction_") || name.find("_argument_") != std::string::npos) {
        return std::nullopt;
    }
    return name;
}

// `contradiction evidence;` written here: this path ends. What follows it
// is not lowered, because the claim is that nothing after it is reached; the
// claim itself is the obligation. The evidence's arguments are the block's
// remaining declarations, each read at the versions current here.
std::optional<Expr> BodyLowering::lower_contradiction(const std::string& marker,
                                                      const std::vector<CXCursor>& statements, std::size_t index,
                                                      const Locals& locals) {
    PathContradiction claim;
    claim.marker = marker;
    for (std::size_t position = index + 1; position < statements.size(); ++position) {
        const std::vector<CXCursor> declared = children_of(statements[position]);
        const std::string expected = marker + "_argument_" + std::to_string(position - index - 1);
        if (clang_getCursorKind(statements[position]) != CXCursor_DeclStmt || declared.size() != 1 ||
            clang_getCursorKind(declared[0]) != CXCursor_VarDecl ||
            take(clang_getCursorSpelling(declared[0])) != expected) {
            return reject("the arguments of this contradiction were not resolved");
        }
        const CXCursor initializer = clang_Cursor_getVarDeclInitializer(declared[0]);
        if (clang_Cursor_isNull(initializer) != 0) {
            return reject("an argument of this contradiction was not resolved");
        }
        claim.operands.push_back(build_expression(initializer, signature, locals, 0));
    }
    consumed_contradictions.push_back(marker);
    Expr ended;
    ended.type = result_type;
    ended.location = presumed_location(clang_getCursorLocation(statements[index]));
    ended.node = std::move(claim);
    return ended;
}

// The one variable a generated declaration statement declares, when it
// declares exactly one and it has this name.
std::optional<CXCursor> BodyLowering::declared_as(CXCursor statement, std::string_view name) {
    if (clang_getCursorKind(statement) != CXCursor_DeclStmt) {
        return std::nullopt;
    }
    const std::vector<CXCursor> declared = children_of(statement);
    if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl ||
        take(clang_getCursorSpelling(declared[0])) != name) {
        return std::nullopt;
    }
    return declared[0];
}

// The name of the block the projector emitted for a case split on this
// path, if the statement at `index` opens one: a generated `bool` followed
// by the split's subject (SPEC.md CASE-017).
std::optional<std::string> BodyLowering::split_marker(const std::vector<CXCursor>& statements,
                                                      std::size_t index) const {
    if (invariant_prefix.empty() || index + 1 >= statements.size() ||
        clang_getCursorKind(statements[index]) != CXCursor_DeclStmt) {
        return std::nullopt;
    }
    const std::vector<CXCursor> declared = children_of(statements[index]);
    if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
        return std::nullopt;
    }
    std::string name = take(clang_getCursorSpelling(declared[0]));
    if (!name.starts_with(invariant_prefix + "split_") || !declared_as(statements[index + 1], name + "_subject")) {
        return std::nullopt;
    }
    return name;
}

// A case split written here: the subject is read at the versions current
// here, and each arm continues this path, through its own nested splits and
// claims and then through the rest of the body after the split. Nothing
// about the representation's states is decided here; the arms are carried
// as written, with each arm's binders standing for the values its case
// exposes, and are matched to the partition when the split is elaborated.
std::optional<Expr> BodyLowering::lower_split(const std::string& marker, const Continuation& from, const Locals& locals,
                                              unsigned depth) {
    const std::vector<CXCursor>& statements = *from.statements;
    std::size_t position = from.index + 1;
    const std::optional<CXCursor> subject = declared_as(statements[position++], marker + "_subject");
    const CXCursor value = subject ? clang_Cursor_getVarDeclInitializer(*subject) : clang_getNullCursor();
    if (clang_Cursor_isNull(value) != 0) {
        return reject("the subject of this case split was not resolved");
    }
    CaseSplit split;
    split.marker = marker;
    ReadSubject read = read_subject(build_expression(value, signature, locals, 0));
    split.operands.push_back(read.read);
    consumed_splits.push_back(Function::SplitSubject{marker, split.operands.front().type});

    // The request that the subject's type be complete computes nothing.
    while (position < statements.size() && clang_getCursorKind(statements[position]) == CXCursor_DeclStmt &&
           std::ranges::all_of(
               children_of(statements[position]),
               [](CXCursor declared) { return clang_getCursorKind(declared) == CXCursor_StaticAssert; })) {
        ++position;
    }

    std::map<std::uint32_t, std::uint32_t> labels;
    const std::string label_prefix = marker + "_label_";
    for (; position < statements.size() && clang_getCursorKind(statements[position]) == CXCursor_DeclStmt; ++position) {
        const std::vector<CXCursor> declared = children_of(statements[position]);
        const std::string name = declared.size() == 1 ? take(clang_getCursorSpelling(declared[0])) : std::string{};
        const CXCursor label =
            name.starts_with(label_prefix) ? clang_Cursor_getVarDeclInitializer(declared[0]) : clang_getNullCursor();
        const std::string arm = name.substr(std::min(name.size(), label_prefix.size()));
        if (clang_Cursor_isNull(label) != 0 || arm.empty() ||
            arm.find_first_not_of("0123456789") != std::string::npos || arm.size() > 5) {
            return reject("a label of this case split was not resolved");
        }
        labels.emplace(static_cast<std::uint32_t>(std::stoul(arm)), static_cast<std::uint32_t>(split.operands.size()));
        split.operands.push_back(build_expression(label, signature, locals, 0));
    }

    for (std::uint32_t arm = 0; position < statements.size(); ++position, ++arm) {
        if (clang_getCursorKind(statements[position]) != CXCursor_CompoundStmt) {
            return reject("an arm of this case split was not resolved");
        }
        const std::vector<CXCursor> contents = children_of(statements[position]);
        if (contents.empty() || !declared_as(contents[0], marker + "_arm_" + std::to_string(arm))) {
            return reject("an arm of this case split was not resolved");
        }
        // The declarations after the arm's own marker are its binders, in
        // the order they were written; its nested splits and claims are
        // blocks.
        Locals bound = locals;
        std::size_t first = 1;
        std::uint32_t binders = 0;
        for (; first < contents.size() && clang_getCursorKind(contents[first]) == CXCursor_DeclStmt; ++first) {
            const std::vector<CXCursor> declared = children_of(contents[first]);
            if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
                return reject("a binder of this case split was not resolved");
            }
            Local binder;
            binder.declaration = declared[0];
            binder.type = convert_type(clang_getCursorType(declared[0]), 0, ReferenceModel::Referent);
            binder.spelling = take(clang_getCursorSpelling(declared[0]));
            binder.binder = CaseBinder{marker, arm, binders++};
            // A binder names a value of its own, as in a proof body
            // (SPEC.md CASE-006): it never repeats a parameter's name or an
            // enclosing arm's binder.
            const auto repeats = [&binder](const Local& other) {
                return other.binder.has_value() && other.spelling == binder.spelling;
            };
            if (std::ranges::any_of(bound, repeats) || std::ranges::any_of(parameters, [&binder](CXCursor parameter) {
                    return take(clang_getCursorSpelling(parameter)) == binder.spelling;
                })) {
                return reject("case binder '" + binder.spelling + "' duplicates an enclosing value name");
            }
            bound.push_back(std::move(binder));
        }
        split.arms.push_back(CaseSplit::Arm{
            labels.contains(arm) ? std::optional<std::uint32_t>{labels.at(arm)} : std::nullopt, binders});
        std::optional<Expr> continued = lower_statements(Continuation{from.outer, &contents, first}, bound, depth + 1);
        if (!continued) {
            return std::nullopt;
        }
        split.operands.push_back(std::move(*continued));
    }
    if (split.arms.empty() || labels.size() > split.arms.size() ||
        std::ranges::any_of(labels, [&split](const auto& label) { return label.first >= split.arms.size(); })) {
        return reject("this case split was not resolved");
    }

    return split_value(std::move(split), statements[from.index], std::move(read));
}

// A subject this body tracks member by member -- a local, a parameter or the
// object a reference parameter designates -- is the value its leaves assemble,
// which has no term until a path binds it. It is bound to a version where the
// split stands, and the split reads that version, so what the split decides is
// decided of the value the leaves hold there (TRUST.md TCB-AGGREGATE-001).
BodyLowering::ReadSubject BodyLowering::read_subject(Expr value) {
    ReadSubject subject;
    if (!std::holds_alternative<Aggregate>(value.node)) {
        subject.read = std::move(value);
        return subject;
    }
    subject.version = next_version++;
    subject.read = value;
    subject.read.node = PlaceRef{*subject.version, anonymous_place("the subject of this case split")};
    subject.assembled = std::move(value);
    return subject;
}

Expr BodyLowering::split_value(CaseSplit split, CXCursor at, ReadSubject subject) {
    Expr result;
    result.type = result_type;
    result.location = presumed_location(clang_getCursorLocation(at));
    result.node = std::move(split);
    if (!subject.version.has_value()) {
        return result;
    }
    return bind(*subject.version, anonymous_place("the subject of this case split"), std::move(subject.assembled),
                std::move(result), at);
}

} // namespace cppl::clangbridge::detail
