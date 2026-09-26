#pragma once

#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/vir/expr.hpp"
#include "lowering.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cppl::obligations::detail {

// What C++ requires of an operation for its behavior to be defined, where the
// requirement is not already met by every value of the operand types (SPEC.md
// 29, 31, Annex T).
enum class Definedness : std::uint8_t {
    // A signed `+`, `-`, `*` or unary `-`: the exact result is a value of the
    // type (ARITH-006, DEFINEDBEHAVIOR-001).
    SignedOverflow,
    // `/` and `%`: the divisor is not zero (ARITH-007, DEFINEDBEHAVIOR-002).
    ZeroDivisor,
    // A signed `/` and `%`: the operands are not the least value and -1
    // (ARITH-007, DEFINEDBEHAVIOR-003).
    QuotientOverflow,
    // A conversion to a signed type that cannot hold every value of the
    // source type: the value is one it holds (ARITH-008).
    Conversion,
};

// One operation an expression evaluates whose behavior is defined only under a
// condition.
struct DefinednessSite {
    const vir::Expr* operation = nullptr;
    Definedness kind = Definedness::SignedOverflow;
    // The conditions of the `?:` that select the operation within its
    // expression, outermost first, each with the outcome under which the
    // operation is evaluated. An operation in an arm C++ does not evaluate
    // owes nothing. `&&` and `||` select by routes and connectives instead
    // (see `collect` in definedness.cpp).
    std::vector<std::pair<const vir::Expr*, bool>> guards;
};

// The operations `expression` evaluates, operands before the operation they
// belong to. A read of a local is not searched: its value was evaluated, and
// owed its conditions, where the local was written. Nor is anything that is not
// a value: the rest of a body under a binding, a loop, a claim.
[[nodiscard]] std::vector<DefinednessSite> definedness_sites(const vir::Expr& expression);

// The first node of `body`, anywhere in it, that is such an operation, with no
// guards: whether and where one exists, never what it owes on a path.
[[nodiscard]] std::optional<DefinednessSite> first_definedness_site(const vir::Expr& body);

// Whether C++ sequences the evaluation of `call` before the site's operation:
// the call is inside one of its operands, or inside a guard that selects it.
[[nodiscard]] bool sequenced_before(const DefinednessSite& site, const vir::Expr& call);

using TermLowerer = std::function<std::expected<kernel::Term, Failure>(const vir::Expr&)>;

// The condition a site owes, supposing each of its guards: `g1 -> ... -> c`.
[[nodiscard]] std::expected<kernel::Proposition, Failure> definedness_condition(const DefinednessSite& site,
                                                                                const TermLowerer& lower);

// What a failed obligation for the site says: the operation, the condition
// and the types, then the rule it rests on.
[[nodiscard]] std::vector<std::string> explain(const DefinednessSite& site);

// The conjunction of the conditions of every operation `expression` would
// evaluate, or nothing when it evaluates none (SPEC.md ARITH-010).
[[nodiscard]] std::expected<std::optional<kernel::Proposition>, Failure> definedness_of(const vir::Expr& expression,
                                                                                        const TermLowerer& lower);

// What a C++ condition in a specification states: that every operation it
// would evaluate is defined, and that it is true. A condition with no such
// operation states exactly its truth, as it always has (SPEC.md ARITH-010).
[[nodiscard]] std::expected<kernel::Proposition, Failure> specified(const vir::Expr& condition,
                                                                    const TermLowerer& lower);

// The same, for a proposition already lowered from `expression`.
[[nodiscard]] std::expected<kernel::Proposition, Failure> specified(const vir::Expr& expression,
                                                                    kernel::Proposition stated,
                                                                    const TermLowerer& lower);

} // namespace cppl::obligations::detail
