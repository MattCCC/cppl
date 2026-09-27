#include "cppl/testing/kernel_oracle.hpp"

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/kernel_generator.hpp"
#include "cppl/testing/kernel_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::testing::kernel_oracle {

namespace k = kernel;
using kernel_generator::Mode;
using kernel_generator::Sample;
using kernel_model::Evaluator;
using kernel_model::Model;
using kernel_model::Truth;
using kernel_model::Value;

namespace {

// The interpretations every accepted goal is evaluated in: carriers of two and
// three elements, under different observation functions.
constexpr std::array<Model, 3> kModels{{
    Model{.seed = 1, .abstract_carrier = 2, .enumerated_width = 4, .budget = 1u << 20},
    Model{.seed = 2, .abstract_carrier = 3, .enumerated_width = 4, .budget = 1u << 20},
    Model{.seed = 3, .abstract_carrier = 1, .enumerated_width = 4, .budget = 1u << 20},
}};

void count_rules(const k::ProofTerm& proof, std::array<bool, kRuleCount>& seen) {
    seen[proof.node.index()] = true;
    std::visit(
        [&](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            const auto into = [&](const k::Box<k::ProofTerm>& inner) {
                count_rules(*inner, seen);
            };
            if constexpr (std::is_same_v<Node, k::ForallIntroduction> ||
                          std::is_same_v<Node, k::ImplicationIntroduction>) {
                into(node.body);
            } else if constexpr (std::is_same_v<Node, k::ForallElimination> ||
                                 std::is_same_v<Node, k::ConjunctionElimination> ||
                                 std::is_same_v<Node, k::DisjunctionIntroduction> ||
                                 std::is_same_v<Node, k::FalsityElimination>) {
                into(node.evidence);
            } else if constexpr (std::is_same_v<Node, k::ImplicationElimination>) {
                into(node.evidence);
                into(node.premise);
            } else if constexpr (std::is_same_v<Node, k::EqualityElimination>) {
                into(node.equality);
                into(node.evidence);
            } else if constexpr (std::is_same_v<Node, k::ConditionalElimination>) {
                into(node.true_case);
                into(node.false_case);
            } else if constexpr (std::is_same_v<Node, k::LinearArithmetic>) {
                for (const k::ArithmeticFact& fact : node.facts) {
                    into(fact.evidence);
                }
            } else if constexpr (std::is_same_v<Node, k::ConjunctionIntroduction>) {
                into(node.left);
                into(node.right);
            } else if constexpr (std::is_same_v<Node, k::DisjunctionElimination>) {
                into(node.evidence);
                into(node.left_case);
                into(node.right_case);
            } else if constexpr (std::is_same_v<Node, k::UnsignedInduction>) {
                into(node.base);
                into(node.step);
            }
        },
        proof.node);
}

std::string show(const Sample& sample) {
    return "\n  mode: " + kernel_generator::describe(sample.mode) + "\n  goal: " + k::describe(sample.goal) +
           "\n  proof: " + k::describe(sample.proof) +
           "\n  definitions: " + std::to_string(sample.context.definition_count());
}

// Every assignment of `locals` the model ranges over, as environments, up to
// `limit` of them. `exhaustive` says whether they are all of them.
std::vector<std::vector<Value>> assignments(const Evaluator& evaluator, std::span<const k::Type> locals,
                                            std::size_t limit, bool& exhaustive) {
    std::vector<std::vector<Value>> environments{{}};
    exhaustive = true;
    for (const k::Type& type : locals) {
        bool complete = false;
        const std::vector<Value> values = evaluator.domain(type, complete);
        exhaustive = exhaustive && complete;
        std::vector<std::vector<Value>> extended;
        for (const auto& environment : environments) {
            for (const Value value : values) {
                if (extended.size() >= limit) {
                    exhaustive = false;
                    break;
                }
                auto next = environment;
                next.push_back(value);
                extended.push_back(std::move(next));
            }
        }
        environments = std::move(extended);
    }
    return environments;
}

std::string show(std::span<const Value> environment) {
    std::string text = "[";
    for (std::size_t index = 0; index < environment.size(); ++index) {
        text += (index == 0 ? "" : ", ") + k::describe(environment[index]);
    }
    return text + "]";
}

} // namespace

std::string rule_name(std::size_t index) {
    static constexpr std::array<const char*, kRuleCount> names{
        "reflexivity",
        "forall-introduction",
        "forall-elimination",
        "hypothesis",
        "implication-introduction",
        "implication-elimination",
        "equality-elimination",
        "conditional-elimination",
        "linear-arithmetic",
        "conjunction-introduction",
        "conjunction-elimination",
        "disjunction-introduction",
        "disjunction-elimination",
        "falsity-elimination",
        "unsigned-induction",
    };
    static_assert(kRuleCount == 15, "a proof former was added: name it here and give the oracle a case for it");
    return index < names.size() ? names[index] : "invalid";
}

std::optional<std::string> examine(const Sample& sample, Statistics& statistics) {
    const auto mode = static_cast<std::size_t>(sample.mode);
    ++statistics.generated[mode];

    const k::CoreLimits limits;
    const k::CheckResult first = k::check(sample.context, sample.goal, sample.proof, limits);
    const k::CheckResult second = k::check(sample.context, sample.goal, sample.proof, limits);
    if (first.has_value() != second.has_value()) {
        return "the kernel's verdict is not deterministic" + show(sample);
    }
    if (!first) {
        if (second.error().kind != first.error().kind || second.error().detail != first.error().detail) {
            return "the kernel's rejection is not deterministic" + show(sample);
        }
        if (first.error().detail.empty()) {
            return "a rejection does not say why" + show(sample);
        }
        return std::nullopt;
    }

    ++statistics.accepted[mode];
    if (!(first->proposition() == sample.goal)) {
        return "the acceptance carries a proposition other than the goal" + show(sample);
    }

    std::array<bool, kRuleCount> seen{};
    count_rules(sample.proof, seen);
    for (std::size_t index = 0; index < kRuleCount; ++index) {
        if (seen[index]) {
            ++statistics.rules[index];
        }
    }

    bool decided = false;
    for (const Model& model : kModels) {
        Evaluator evaluator(sample.context, model);
        const Truth truth = evaluator.truth(sample.goal);
        if (truth == Truth::False) {
            return "SOUNDNESS: the kernel accepted a goal that is false in the model (seed " +
                   std::to_string(model.seed) + ", carrier " + std::to_string(model.abstract_carrier) + ")" +
                   show(sample);
        }
        decided = decided || truth == Truth::True;
    }
    if (decided) {
        ++statistics.decided[mode];
    }
    return std::nullopt;
}

std::optional<std::string> examine_terms(kernel_generator::Choices& choices, Statistics& statistics) {
    const kernel_generator::TermSample sample = kernel_generator::generate_term(choices);
    ++statistics.terms;
    const k::CoreLimits limits;
    const auto describe_sample = [&] {
        return "\n  term: " + k::describe(sample.term) + "\n  proposition: " + k::describe(sample.proposition) +
               "\n  argument: " + k::describe(sample.argument) + "\n  binders: " + std::to_string(sample.locals.size());
    };

    const auto typed = k::type_of(sample.context, sample.locals, sample.term, limits);
    if (!typed) {
        return "a generated term does not type-check: " + typed.error().detail + describe_sample();
    }
    const auto normal = k::normalize(sample.context, sample.term, limits);
    if (!normal) {
        // A budget or depth limit is a refusal, never a wrong answer.
        return std::nullopt;
    }
    const auto normal_type = k::type_of(sample.context, sample.locals, *normal, limits);
    if (!normal_type || !(*normal_type == *typed)) {
        return "normalization changed a term's type" + describe_sample() + "\n  normal: " + k::describe(*normal);
    }
    const auto again = k::normalize(sample.context, *normal, limits);
    if (!again || !(*again == *normal)) {
        return "normalization is not idempotent" + describe_sample() + "\n  normal: " + k::describe(*normal);
    }

    const std::span<const k::Type> outer(sample.locals.data(), sample.locals.size() - 1);
    const k::Term instantiated = k::instantiate(sample.term, sample.argument);
    const k::Proposition instantiated_proposition = k::instantiate(sample.proposition, sample.argument);
    const auto instantiated_type = k::type_of(sample.context, outer, instantiated, limits);
    if (!instantiated_type || !(*instantiated_type == *typed)) {
        return "substitution changed a term's type" + describe_sample();
    }
    // A new binder inserted `cutoff` binders in: every variable that reaches
    // past it moves out by one.
    const auto cutoff = choices.below(static_cast<std::uint32_t>(sample.locals.size() + 1));
    const std::size_t inserted_at = sample.locals.size() - cutoff;
    const k::Term shifted = k::shift(sample.term, 1, cutoff);
    const k::Proposition shifted_proposition = k::shift(sample.proposition, 1, cutoff);

    for (const Model& model : kModels) {
        Evaluator evaluator(sample.context, model);
        bool exhaustive = false;
        const auto environments = assignments(evaluator, sample.locals, 512, exhaustive);
        for (const auto& environment : environments) {
            ++statistics.term_assignments;
            const auto value = evaluator.value(sample.term, environment);
            if (!value) {
                continue;
            }
            const auto reduced = evaluator.value(*normal, environment);
            if (reduced && *reduced != *value) {
                return "normalization changed a term's value at " + show(environment) + describe_sample() +
                       "\n  normal: " + k::describe(*normal);
            }

            // Substitution: the innermost binder given the argument's value.
            const std::span<const Value> outside(environment.data(), environment.size() - 1);
            const auto argument = evaluator.value(sample.argument, outside);
            if (argument && *argument == environment.back()) {
                const auto substituted = evaluator.value(instantiated, outside);
                if (substituted && *substituted != *value) {
                    return "substitution changed a term's value at " + show(environment) + describe_sample();
                }
                const Truth before = evaluator.truth(sample.proposition, environment);
                const Truth after = evaluator.truth(instantiated_proposition, outside);
                if (before != Truth::Unknown && after != Truth::Unknown && before != after) {
                    return "substitution changed a proposition's truth at " + show(environment) + describe_sample();
                }
            }

            // Shifting: an extra binder, of any value.
            std::vector<Value> widened(environment.begin(), environment.end());
            widened.insert(widened.begin() + static_cast<std::ptrdiff_t>(inserted_at), Value{0});
            const auto moved = evaluator.value(shifted, widened);
            if (moved && *moved != *value) {
                return "shifting changed a term's value at " + show(environment) + describe_sample();
            }
            const Truth before = evaluator.truth(sample.proposition, environment);
            const Truth after = evaluator.truth(shifted_proposition, widened);
            if (before != Truth::Unknown && after != Truth::Unknown && before != after) {
                return "shifting changed a proposition's truth at " + show(environment) + describe_sample();
            }
        }
    }
    return std::nullopt;
}

} // namespace cppl::testing::kernel_oracle
