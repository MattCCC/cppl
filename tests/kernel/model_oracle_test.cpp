// Whatever the kernel accepts is true in an independent finite model of its
// core (docs/KERNEL.md, "Testing"; TRUST.md TCB-CORE-001, TCB-TEST-001).
//
// The derivations come from tests/support/kernel: built rule by rule, with a
// premise either derived or supposed and a deliberate defect at some steps. The
// untrusted refutation search and automation supply candidate evidence for
// arithmetic goals. The kernel decides; kernel_model says whether what it
// accepted can be false. A fixed seed makes every run the same run, and the
// persistent fuzz targets in tests/fuzz search the same space from bytes.
//
// SPEC: SOUND-001 PROOF-001 EQ-001

#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/kernel_generator.hpp"
#include "cppl/testing/kernel_model.hpp"
#include "cppl/testing/kernel_oracle.hpp"
#include "cppl/testing/test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>

namespace {

namespace k = cppl::kernel;
namespace gen = cppl::testing::kernel_generator;
namespace model = cppl::testing::kernel_model;
namespace oracle = cppl::testing::kernel_oracle;
using model::Truth;
using model::Wide;

k::IntType u(std::uint16_t width) {
    return k::IntType{width, k::Signedness::Unsigned};
}
k::IntType s(std::uint16_t width) {
    return k::IntType{width, k::Signedness::Signed};
}

k::Term var(std::uint32_t index) {
    return k::Term::variable(k::VarIndex{index});
}

k::Term lit(k::IntType type, Wide value) {
    return k::Term::literal(type, value);
}

void report(const oracle::Statistics& statistics) {
    for (std::size_t mode = 0; mode < gen::kModeCount; ++mode) {
        std::cout << gen::describe(static_cast<gen::Mode>(mode)) << ": " << statistics.generated[mode] << " generated, "
                  << statistics.accepted[mode] << " accepted, " << statistics.decided[mode] << " decided true\n";
    }
    for (std::size_t rule = 0; rule < oracle::kRuleCount; ++rule) {
        std::cout << "  " << oracle::rule_name(rule) << ": " << statistics.rules[rule] << '\n';
    }
}

void run(gen::Mode mode, std::uint64_t seed, unsigned samples, oracle::Statistics& statistics) {
    for (unsigned sample = 0; sample < samples; ++sample) {
        gen::SeededChoices choices(seed * 1000003u + sample);
        const gen::Sample generated = gen::generate(choices, mode);
        if (const auto violation = oracle::examine(generated, statistics)) {
            std::cerr << "sample " << sample << " of seed " << seed << ": " << *violation << '\n';
            CPPL_CHECK(!violation.has_value());
        }
    }
}

} // namespace

// The oracle is only as good as the model, so the model's machine arithmetic
// is first checked against the host's own on every pair of 8-bit values.
CPPL_TEST(the_model_computes_machine_arithmetic_as_the_host_does) {
    for (int a = -128; a < 128; ++a) {
        for (int b = -128; b < 128; ++b) {
            const auto ua = static_cast<std::uint8_t>(a);
            const auto ub = static_cast<std::uint8_t>(b);
            CPPL_CHECK(model::machine::add(s(8), a, b) == static_cast<std::int8_t>(ua + ub));
            CPPL_CHECK(model::machine::subtract(s(8), a, b) == static_cast<std::int8_t>(ua - ub));
            CPPL_CHECK(model::machine::multiply(s(8), a, b) == static_cast<std::int8_t>(ua * ub));
            CPPL_CHECK(model::machine::add(u(8), ua, ub) == static_cast<std::uint8_t>(ua + ub));
            CPPL_CHECK(model::machine::multiply(u(8), ua, ub) == static_cast<std::uint8_t>(ua * ub));
            if (b != 0) {
                const int quotient = a / b;
                CPPL_CHECK(model::machine::quotient(s(8), a, b) == static_cast<std::int8_t>(quotient));
                CPPL_CHECK(model::machine::remainder(a, b) == a - quotient * b);
                CPPL_CHECK(model::machine::quotient(u(8), ua, ub) == ua / ub);
                CPPL_CHECK(model::machine::remainder(ua, ub) == ua % ub);
            } else {
                CPPL_CHECK(model::machine::quotient(s(8), a, b) == 0);
                CPPL_CHECK(model::machine::remainder(a, b) == a);
            }
            CPPL_CHECK(model::machine::fits(s(8), k::PrimOp::AddFits, a, b) == (a + b >= -128 && a + b <= 127));
            CPPL_CHECK(model::machine::fits(s(8), k::PrimOp::SubFits, a, b) == (a - b >= -128 && a - b <= 127));
            CPPL_CHECK(model::machine::fits(s(8), k::PrimOp::MulFits, a, b) == (a * b >= -128 && a * b <= 127));
        }
    }
    const Wide greatest = (Wide{1} << 64) - 1;
    CPPL_CHECK(model::machine::multiply(u(64), greatest, greatest) == 1);
    CPPL_CHECK(model::machine::quotient(s(64), -(Wide{1} << 63), -1) == -(Wide{1} << 63));
    CPPL_CHECK(model::machine::remainder(-(Wide{1} << 63), -1) == 0);
    CPPL_CHECK(model::machine::wrap(s(64), Wide{1} << 63) == -(Wide{1} << 63));
    CPPL_CHECK(!model::machine::fits(u(64), k::PrimOp::MulFits, greatest, greatest));
}

// It decides what it can decide, and says Unknown about what it only samples.
CPPL_TEST(the_model_is_three_valued_and_never_guesses) {
    const k::Context context;
    model::Evaluator evaluator(context, model::Model{});
    const auto small = k::Type{u(2)};
    const auto wide = k::Type{u(32)};
    const auto zero = [](k::IntType type) {
        return lit(type, 0);
    };

    CPPL_CHECK(evaluator.truth(k::Proposition::for_all(small, k::Proposition::equality(small, var(0), var(0)))) ==
               Truth::True);
    CPPL_CHECK(evaluator.truth(k::Proposition::for_all(small, k::Proposition::equality(small, var(0), zero(u(2))))) ==
               Truth::False);
    CPPL_CHECK(evaluator.truth(k::Proposition::falsity()) == Truth::False);
    CPPL_CHECK(evaluator.truth(k::Proposition::implication(k::Proposition::falsity(), k::Proposition::falsity())) ==
               Truth::True);
    // Over a sampled type, a counterexample among the samples decides; their
    // absence does not.
    CPPL_CHECK(evaluator.truth(k::Proposition::for_all(wide, k::Proposition::equality(wide, var(0), var(0)))) ==
               Truth::Unknown);
    CPPL_CHECK(evaluator.truth(k::Proposition::for_all(wide, k::Proposition::equality(wide, var(0), zero(u(32))))) ==
               Truth::False);
    // A sampled premise decides nothing about the implication it supposes.
    const auto sampled = k::Proposition::for_all(wide, k::Proposition::equality(wide, var(0), var(0)));
    CPPL_CHECK(evaluator.truth(k::Proposition::implication(sampled, k::Proposition::falsity())) == Truth::Unknown);
    // An abstract value is one of a carrier of elements, and two of them may
    // differ.
    const auto value = k::Type::value("V", {k::Type{s(4)}});
    CPPL_CHECK(evaluator.truth(k::Proposition::for_all(
                   value, k::Proposition::for_all(value, k::Proposition::equality(value, var(0), var(1))))) ==
               Truth::False);
    // Observations are functions: equal subjects give equal observations.
    CPPL_CHECK(evaluator.truth(k::Proposition::for_all(
                   value, k::Proposition::equality(k::Type{s(4)}, k::Term::project(value, 0, var(0)),
                                                   k::Term::project(value, 0, var(0))))) == Truth::True);
}

CPPL_TEST(accepted_derivations_are_true_in_every_interpretation) {
    oracle::Statistics statistics;
    run(gen::Mode::Derivation, 1, 6000, statistics);
    report(statistics);
    const auto mode = static_cast<std::size_t>(gen::Mode::Derivation);
    // A generator whose output the kernel rejects wholesale would make this
    // test pass without testing anything. It must reach acceptance often, and
    // through every rule.
    CPPL_CHECK(statistics.accepted[mode] * 5 >= statistics.generated[mode]);
    // Nor may it only produce what is accepted: defects are part of the input.
    CPPL_CHECK(statistics.accepted[mode] < statistics.generated[mode]);
    CPPL_CHECK(statistics.decided[mode] * 4 >= statistics.accepted[mode]);
    for (std::size_t rule = 0; rule < oracle::kRuleCount; ++rule) {
        if (statistics.rules[rule] == 0) {
            std::cerr << "no accepted derivation used " << oracle::rule_name(rule) << '\n';
        }
        CPPL_CHECK(statistics.rules[rule] > 0);
    }
}

CPPL_TEST(accepted_arithmetic_is_true_in_every_interpretation) {
    oracle::Statistics statistics;
    run(gen::Mode::Arithmetic, 2, 3000, statistics);
    report(statistics);
    const auto mode = static_cast<std::size_t>(gen::Mode::Arithmetic);
    CPPL_CHECK(statistics.accepted[mode] * 10 >= statistics.generated[mode]);
    CPPL_CHECK(statistics.decided[mode] * 2 >= statistics.accepted[mode]);
}

CPPL_TEST(automation_cannot_make_the_kernel_accept_a_false_goal) {
    oracle::Statistics statistics;
    run(gen::Mode::Automation, 3, 1500, statistics);
    report(statistics);
    const auto mode = static_cast<std::size_t>(gen::Mode::Automation);
    CPPL_CHECK(statistics.accepted[mode] * 10 >= statistics.generated[mode]);
}

CPPL_TEST(normalization_substitution_and_shifting_preserve_meaning) {
    oracle::Statistics statistics;
    for (unsigned sample = 0; sample < 3000; ++sample) {
        gen::SeededChoices choices(0x51ed270b + sample);
        if (const auto violation = oracle::examine_terms(choices, statistics)) {
            std::cerr << "term sample " << sample << ": " << *violation << '\n';
            CPPL_CHECK(!violation.has_value());
        }
    }
    std::cout << statistics.terms << " terms at " << statistics.term_assignments << " assignments\n";
    CPPL_CHECK(statistics.term_assignments > statistics.terms);
}

// The byte-driven choices the fuzz targets use terminate on any input,
// including none, and decide the same sample from the same bytes.
CPPL_TEST(byte_choices_are_total_and_deterministic) {
    constexpr std::array<std::uint8_t, 9> bytes{3, 200, 17, 0, 255, 9, 42, 42, 1};
    const std::span<const std::uint8_t> all(bytes);
    for (std::size_t length = 0; length <= all.size(); ++length) {
        for (std::uint32_t mode = 0; mode < gen::kModeCount; ++mode) {
            gen::ByteChoices first(all.first(length));
            gen::ByteChoices second(all.first(length));
            const auto a = gen::generate(first, static_cast<gen::Mode>(mode));
            const auto b = gen::generate(second, static_cast<gen::Mode>(mode));
            CPPL_CHECK(a.goal == b.goal);
            CPPL_CHECK(a.proof == b.proof);
        }
    }
}
