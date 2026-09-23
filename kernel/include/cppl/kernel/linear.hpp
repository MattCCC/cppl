#pragma once

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cppl::kernel {

// sum(coefficient * variable) + constant <= 0, over the integers. Variables
// are listed in increasing order, each with a nonzero coefficient.
struct LinearConstraint {
    std::vector<std::pair<std::uint32_t, Wide>> terms;
    Wide constant = 0;

    friend bool operator==(const LinearConstraint&, const LinearConstraint&) = default;
};

enum class VariableRole : std::uint8_t {
    // The machine value of `term`: an integer within the bounds of its type.
    Value,
    // How many multiples of 2^width separate `term`'s polynomial, read over
    // the integers, from the machine value it denotes.
    Wrap,
};

struct ArithmeticVariable {
    VariableRole role = VariableRole::Value;
    IntType type;
    Term term;
    // For a Wrap variable, the least and greatest multiple the bounds of the
    // factors allow. It is a hint to producers; the kernel checks nothing
    // against it.
    Wide lowest = 0;
    Wide highest = 0;
};

// The facts of an arithmetic step, and the negation of its goal, stated as
// integer linear constraints. The goal holds exactly when no integer
// assignment satisfies every constraint and one member of every disjunction.
struct ArithmeticSystem {
    std::vector<ArithmeticVariable> variables;
    std::vector<LinearConstraint> constraints;
    std::vector<std::array<LinearConstraint, 2>> disjunctions;
};

// Translates facts and a negated goal, each an equality between well-typed
// terms, into an arithmetic system (SPEC.md 7.5). A goal of `False` contributes
// no constraint: its negation holds outright.
//
// Every term is normalized and read as a polynomial of its machine type. A
// monomial becomes a variable bounded by that type; a polynomial that is not a
// single monomial becomes its integer reading minus a fresh multiple of
// 2^width, bounded by the type as well. That is an exact account of
// two's-complement arithmetic, so nothing true of the machine is lost and
// nothing false is added. A comparison evaluating to one or zero becomes the
// order it states or its negation; any other equality is equality of values.
[[nodiscard]] std::expected<ArithmeticSystem, CoreError> arithmetic_system(const Context& context,
                                                                           std::span<const Proposition> facts,
                                                                           const Proposition& goal,
                                                                           const CoreLimits& limits);

// Whether `certificate` shows that `system` has no integer solution.
[[nodiscard]] std::expected<void, std::string> refutes(const ArithmeticSystem& system,
                                                       const ArithmeticCertificate& certificate,
                                                       const CoreLimits& limits);

} // namespace cppl::kernel
