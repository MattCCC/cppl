#include "cppl/testing/kernel_generator.hpp"

#include "cppl/automation/evidence.hpp"
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "kernel_generator_detail.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::testing::kernel_generator {

using detail::Derivation;
using detail::Generator;
using detail::indexed_a;
using detail::kBoolean;
using detail::Scope;
using detail::value_v;
using detail::value_w;

std::uint32_t ByteChoices::below(std::uint32_t bound) {
    if (bound <= 1) {
        return 0;
    }
    std::uint32_t value = 0;
    // One byte for a small choice, two for a larger one, so a byte stream of
    // a given length always decides about the same amount.
    const std::size_t width = bound <= 256 ? 1 : 2;
    for (std::size_t byte = 0; byte < width; ++byte) {
        if (position_ >= bytes_.size()) {
            return 0;
        }
        value = (value << 8) | bytes_[position_++];
    }
    return value % bound;
}

std::uint32_t SeededChoices::below(std::uint32_t bound) {
    if (bound <= 1) {
        return 0;
    }
    state_ ^= state_ << 13;
    state_ ^= state_ >> 7;
    state_ ^= state_ << 17;
    return static_cast<std::uint32_t>(state_ % bound);
}

std::string describe(Mode mode) {
    switch (mode) {
        case Mode::Derivation:
            return "derivation";
        case Mode::Arithmetic:
            return "arithmetic";
        case Mode::Automation:
            return "automation";
    }
    return "invalid";
}

k::ProofTerm lift(const k::ProofTerm& proof, std::uint32_t amount, std::uint32_t cutoff) {
    const auto again = [&](const k::Box<k::ProofTerm>& inner, std::uint32_t at) {
        return k::Box<k::ProofTerm>{lift(*inner, amount, at)};
    };
    return std::visit(
        [&](const auto& node) -> k::ProofTerm {
            using Node = std::decay_t<decltype(node)>;
            Node copy = node;
            if constexpr (std::is_same_v<Node, k::Hypothesis>) {
                if (copy.index.value >= cutoff) {
                    copy.index.value += amount;
                }
            } else if constexpr (std::is_same_v<Node, k::ForallIntroduction>) {
                copy.body = again(node.body, cutoff);
            } else if constexpr (std::is_same_v<Node, k::ImplicationIntroduction>) {
                // The premise this introduces is index 0 underneath it.
                copy.body = again(node.body, cutoff + 1);
            } else if constexpr (std::is_same_v<Node, k::ImplicationElimination>) {
                copy.evidence = again(node.evidence, cutoff);
                copy.premise = again(node.premise, cutoff);
            } else if constexpr (std::is_same_v<Node, k::EqualityElimination>) {
                copy.equality = again(node.equality, cutoff);
                copy.evidence = again(node.evidence, cutoff);
            } else if constexpr (std::is_same_v<Node, k::ConditionalElimination>) {
                copy.true_case = again(node.true_case, cutoff);
                copy.false_case = again(node.false_case, cutoff);
            } else if constexpr (std::is_same_v<Node, k::LinearArithmetic>) {
                for (k::ArithmeticFact& fact : copy.facts) {
                    fact.evidence = again(fact.evidence, cutoff);
                }
            } else if constexpr (std::is_same_v<Node, k::ConjunctionIntroduction>) {
                copy.left = again(node.left, cutoff);
                copy.right = again(node.right, cutoff);
            } else if constexpr (std::is_same_v<Node, k::ForallElimination> ||
                                 std::is_same_v<Node, k::ConjunctionElimination> ||
                                 std::is_same_v<Node, k::DisjunctionIntroduction> ||
                                 std::is_same_v<Node, k::FalsityElimination>) {
                copy.evidence = again(node.evidence, cutoff);
            } else if constexpr (std::is_same_v<Node, k::DisjunctionElimination>) {
                copy.evidence = again(node.evidence, cutoff);
                copy.left_case = again(node.left_case, cutoff);
                copy.right_case = again(node.right_case, cutoff);
            } else if constexpr (std::is_same_v<Node, k::UnsignedInduction>) {
                // Both premises are checked under the assumptions standing at
                // the induction; neither introduces one.
                copy.base = again(node.base, cutoff);
                copy.step = again(node.step, cutoff);
            } else {
                static_assert(std::is_same_v<Node, k::Reflexivity>);
            }
            return k::ProofTerm{std::move(copy)};
        },
        proof.node);
}

namespace {

constexpr std::uint32_t kPerturbOdds = 10;

} // namespace

namespace detail {

k::Type integer(std::uint16_t width, k::Signedness signedness) {
    return k::Type::integer(width, signedness);
}

// The abstract types every sample may quantify over.
k::Type value_v() {
    return k::Type::value("V", {integer(4, k::Signedness::Signed), integer(2, k::Signedness::Unsigned)});
}
k::Type value_w() {
    return k::Type::value("W", {value_v(), integer(8, k::Signedness::Signed)});
}
k::Type indexed_a() {
    return k::Type::indexed(integer(4, k::Signedness::Signed), 4);
}

} // namespace detail

Sample generate(Choices& choices, Mode mode) {
    Generator generator(choices);
    return generator.run(mode);
}

TermSample generate_term(Choices& choices) {
    Generator generator(choices);
    return generator.term_sample();
}

Sample Generator::run(Mode mode) {
    define_some();
    Sample sample{context_, k::Proposition::falsity(), k::ProofTerm::reflexivity(), mode};
    switch (mode) {
        case Mode::Derivation: {
            Scope scope;
            Derivation derived = derive(scope, 1 + choices_.below(5));
            sample.goal = std::move(derived.proposition);
            sample.proof = std::move(derived.proof);
            break;
        }
        case Mode::Arithmetic: {
            Derivation derived = arithmetic();
            sample.goal = std::move(derived.proposition);
            sample.proof = std::move(derived.proof);
            break;
        }
        case Mode::Automation: {
            sample.goal = automation_goal();
            if (auto proposed = automation::propose(context_, sample.goal)) {
                sample.proof = std::move(proposed->proof);
            }
            break;
        }
    }
    sample.context = context_;
    return sample;
}

std::uint32_t Generator::below(std::uint32_t bound) {
    return choices_.below(bound);
}

bool Generator::one_in(std::uint32_t odds) {
    return choices_.one_in(odds);
}

bool Generator::perturb() {
    return one_in(kPerturbOdds);
}

std::uint64_t Generator::bits64() {
    std::uint64_t value = 0;
    for (int part = 0; part < 4; ++part) {
        value = (value << 16) | below(1u << 16);
    }
    return value;
}

k::IntType Generator::small_integer() {
    static constexpr std::uint16_t widths[] = {2, 3, 4, 4, 1};
    const std::uint16_t width = widths[below(5)];
    return k::IntType{width, width == 1 || one_in(2) ? k::Signedness::Unsigned : k::Signedness::Signed};
}

k::IntType Generator::any_integer() {
    if (!one_in(4)) {
        return small_integer();
    }
    static constexpr std::uint16_t widths[] = {8, 16, 32, 64};
    return k::IntType{widths[below(4)], one_in(2) ? k::Signedness::Unsigned : k::Signedness::Signed};
}

k::Type Generator::binder() {
    switch (below(10)) {
        case 0:
            return value_v();
        case 1:
            return indexed_a();
        case 2:
            return one_in(2) ? value_w() : as_type(any_integer());
        default:
            return as_type(small_integer());
    }
}

k::Type Generator::as_type(const k::IntType& type) {
    return k::Type{type};
}

TermSample Generator::term_sample() {
    define_some();
    TermSample sample{
        context_, {}, k::Term::literal(kBoolean, 0), k::Proposition::falsity(), k::Term::literal(kBoolean, 0)};
    const std::uint32_t binders = 1 + below(3);
    for (std::uint32_t index = 0; index < binders; ++index) {
        sample.locals.push_back(one_in(5) ? (one_in(2) ? value_v() : indexed_a()) : as_type(small_integer()));
    }
    const k::IntType at = one_in(3) ? any_integer() : small_integer();
    sample.term = integer_term(sample.locals, at, 1 + below(4));
    sample.proposition = proposition(sample.locals, 2);
    const std::span<const k::Type> outer(sample.locals.data(), sample.locals.size() - 1);
    auto replacement = term(outer, sample.locals.back(), 2);
    if (!replacement) {
        // No term of an abstract type can be formed without a variable of
        // it, so the innermost binder becomes an integer.
        sample.locals.back() = as_type(small_integer());
        sample.term = integer_term(sample.locals, at, 2);
        sample.proposition = proposition(sample.locals, 2);
        // proposition() may have reallocated the locals `outer` viewed.
        const std::span<const k::Type> kept(sample.locals.data(), sample.locals.size() - 1);
        replacement = integer_term(kept, sample.locals.back().integer_type(), 2);
    }
    sample.argument = std::move(*replacement);
    sample.context = context_;
    return sample;
}

} // namespace cppl::testing::kernel_generator
