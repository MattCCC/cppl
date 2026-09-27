#pragma once

// An independent finite model of the kernel's formal core (docs/KERNEL.md).
//
// It gives every well-formed term a value and every well-formed proposition a
// truth value, by evaluation, never by the kernel's own normalization,
// substitution or arithmetic. It shares with the kernel only the data
// structures it reads and `describe(Type)`, which names a type for the model to
// interpret.
//
// Soundness of the kernel means that a proposition it accepts is true in every
// model of the core. This is one family of such models: machine integers are
// exactly the machine's, and each abstract or indexed type is a small carrier
// whose observations are fixed pseudo-random functions chosen by `seed`. So a
// proposition the kernel accepts and this model finds false is a soundness
// defect, whichever interpretation found it (TRUST.md TCB-CORE-001,
// AGENTS.md 8).
//
// Truth is three-valued. A quantifier over a type too wide to enumerate is
// evaluated at sampled values, and evaluation that exceeds its budget gives up:
// both give `Unknown` where the answer is not certain, never `True` or `False`
// by guessing. `False` is reported only with a counterexample in hand, so the
// oracle `accepted -> not False` is exact for what it reports.

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cppl::testing::kernel_model {

using Wide = kernel::Wide;

enum class Truth : std::uint8_t { False, True, Unknown };

std::string describe(Truth truth);

// One interpretation of the abstract types, and the limits evaluation keeps to.
struct Model {
    // Chooses the observations of abstract and indexed values.
    std::uint64_t seed = 0;
    // Elements of every abstract and indexed type's carrier.
    std::uint32_t abstract_carrier = 3;
    // Integer types at most this wide are enumerated exactly; wider ones are
    // sampled at their edges and at a few seeded values.
    std::uint16_t enumerated_width = 4;
    // Term nodes evaluated, in total, before evaluation gives up.
    std::uint64_t budget = 1u << 22;
};

// A value of some core type. An integer is its mathematical value, always
// within its type's range; an abstract or indexed value is an element of the
// type's carrier, numbered from zero.
using Value = Wide;

class Evaluator {
  public:
    Evaluator(const kernel::Context& context, Model model);

    // The value of `term` where `environment` gives each enclosing binder's
    // value, outermost first: Var{0} is environment.back(). None when the term
    // is malformed or the budget is spent.
    [[nodiscard]] std::optional<Value> value(const kernel::Term& term, std::span<const Value> environment);

    // The truth of `proposition` where `environment` gives its free binders'
    // values, as for value().
    [[nodiscard]] Truth truth(const kernel::Proposition& proposition, std::span<const Value> environment = {});

    // The values a quantifier over `type` ranges over in this model, and
    // whether they are all of them.
    [[nodiscard]] std::vector<Value> domain(const kernel::Type& type, bool& exhaustive) const;

    [[nodiscard]] bool exhausted() const noexcept {
        return spent_ > model_.budget;
    }

  private:
    [[nodiscard]] std::optional<Value> evaluate(const kernel::Term& term, std::span<const Value> environment,
                                                std::size_t depth);

    [[nodiscard]] std::optional<Value> observe(const kernel::Type& domain, std::uint64_t selector, Value subject,
                                               const kernel::Type& result) const;

    const kernel::Context& context_;
    Model model_;
    std::uint64_t spent_ = 0;
};

// Machine semantics, restated here from FOUNDATIONS.md and SPEC.md 7.1 rather
// than taken from the kernel, which is what they check.
namespace machine {

[[nodiscard]] Wide lowest(const kernel::IntType& type);
[[nodiscard]] Wide highest(const kernel::IntType& type);
// `value` reduced into `type` by two's complement.
[[nodiscard]] Wide wrap(const kernel::IntType& type, Wide value);
[[nodiscard]] Wide add(const kernel::IntType& type, Wide lhs, Wide rhs);
[[nodiscard]] Wide subtract(const kernel::IntType& type, Wide lhs, Wide rhs);
[[nodiscard]] Wide multiply(const kernel::IntType& type, Wide lhs, Wide rhs);
// Truncating division made total: x / 0 is 0, and a quotient the type cannot
// hold wraps.
[[nodiscard]] Wide quotient(const kernel::IntType& type, Wide lhs, Wide rhs);
// x - (x / y) * y with the exact quotient; x % 0 is x.
[[nodiscard]] Wide remainder(Wide lhs, Wide rhs);
// Whether the exact sum, difference or product is a value of `type`.
[[nodiscard]] bool fits(const kernel::IntType& type, kernel::PrimOp op, Wide lhs, Wide rhs);

} // namespace machine

} // namespace cppl::testing::kernel_model
