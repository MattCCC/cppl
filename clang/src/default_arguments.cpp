#include "default_arguments.hpp"

#include "cppl/clang/ast.hpp"
#include "expressions.hpp"
#include "places.hpp"
#include "signature.hpp"
#include "types.hpp"

#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// The default arguments a call relies on (SPEC.md R.16): which argument Clang
// supplied from a default, the default the declaration states, and its lowering
// where the call stands.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::take;

namespace {

// Marks every part of a lowered default argument as standing in it: each call
// it makes names `owner` as the default it comes from, and each construct it
// holds that is not modeled is refused as the default of `owner`, since the
// call relying on it does not show it (SPEC.md R.16).
void attribute_to_default(Expr& expression, const std::string& owner, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        expression.node = Unsupported{"the default argument of " + owner + " nests deeper than the bridge allows"};
        return;
    }
    if (auto* refused = std::get_if<Unsupported>(&expression.node)) {
        refused->reason = "the default argument of " + owner +
                          ", on which this call relies, is not modeled: " + refused->reason + " (SPEC.md R.16)";
        return;
    }
    if (auto* call = std::get_if<Call>(&expression.node); call != nullptr && call->default_argument.empty()) {
        call->default_argument = owner;
    }
    std::visit(
        [&](auto& node) {
            if constexpr (requires { node.operands; }) {
                for (Expr& child : node.operands)
                    attribute_to_default(child, owner, depth + 1);
            }
            if constexpr (requires { node.arguments; }) {
                for (Expr& child : node.arguments)
                    attribute_to_default(child, owner, depth + 1);
            }
            if constexpr (requires { node.extent; }) {
                for (Expr& child : node.extent)
                    attribute_to_default(child, owner, depth + 1);
            }
            if constexpr (requires { node.body; }) {
                for (Expr& child : node.body)
                    attribute_to_default(child, owner, depth + 1);
            }
        },
        expression.node);
}

} // namespace

// Whether `argument`, an argument of a call, is the default argument of its
// parameter, which Clang supplies where the call writes none (SPEC.md R.16,
// EDGECASE-038). libclang exposes that node as an unexposed expression with no
// children and no extent: nothing at the call wrote it, and the expression it
// stands for is the parameter's.
bool is_default_argument(CXCursor argument) {
    return clang_getCursorKind(argument) == CXCursor_UnexposedExpr &&
           clang_Range_isNull(clang_getCursorExtent(argument)) != 0 && children_of(argument).empty();
}

// The parameter at `index` of `callee` whose default argument a call relies
// on, as a diagnostic names it: "parameter 'p' of 'f'".
std::string default_owner(CXCursor callee, unsigned index) {
    const std::string name = clang_Cursor_getNumArguments(callee) > static_cast<int>(index)
                                 ? take(clang_getCursorSpelling(clang_Cursor_getArgument(callee, index)))
                                 : std::string();
    return (name.empty() ? "parameter " + std::to_string(index + 1) : "parameter '" + name + "'") + " of '" +
           qualified_name_of(callee) + "'";
}

// The expression a call relies on for its argument at `index`, which Clang
// supplied from its parameter's default (`is_default_argument`), or why it is
// not one this implementation evaluates at the call (SPEC.md R.16).
//
// It is the default the declaration the call names states, as Clang resolved
// it for the call: a default an earlier declaration states is the later one's
// too, and a template's is instantiated at the specialization called before
// the call uses it. A call's arguments stand at its callee's parameter
// positions, as every argument the bridge lowers does.
std::expected<CXCursor, std::string> default_argument_of(CXCursor callee, unsigned index) {
    const CXCursor initializer = clang_Cursor_getNumArguments(callee) > static_cast<int>(index)
                                     ? clang_Cursor_getVarDeclInitializer(clang_Cursor_getArgument(callee, index))
                                     : clang_getNullCursor();
    if (clang_Cursor_isNull(initializer) != 0) {
        return std::unexpected("the default argument of " + default_owner(callee, index) + " was not resolved");
    }
    return initializer;
}

// A default argument a call relies on, lowered as the expression its callee's
// declaration states, evaluated where the call stands, before the call, exactly
// as if the caller had written it there (SPEC.md R.16, CONTRACTCOMP-002,
// EDGECASE-038). Whatever it calls owes at this call what any call owes, and the
// callee's contract, its refined parameters and its measure meet its value as
// they meet any argument's.
//
// C++ lets a default argument use no parameter, no local and no `this`
// ([dcl.fct.default]), so it is lowered with none of the caller's in scope:
// anything of the caller's state it would read is refused, never read in place
// of what C++ reads.
Expr lower_default_argument(CXCursor call, CXCursor callee, unsigned index, CXCursor argument,
                            const Signature& signature, unsigned depth) {
    const std::expected<CXCursor, std::string> initializer = default_argument_of(callee, index);
    if (!initializer) {
        Expr refused = unsupported_expression(call, initializer.error() + " (SPEC.md R.16)");
        refused.type = convert_type(clang_getCursorType(argument));
        return refused;
    }
    const Signature declaration_scope{
        {}, std::nullopt, signature.clause, signature.refinements, signature.unsafe_effects};
    Expr lowered = build_expression(*initializer, declaration_scope, Locals{}, depth + 1);
    attribute_to_default(lowered, default_owner(callee, index), 0);
    return lowered;
}

} // namespace cppl::clangbridge::detail
