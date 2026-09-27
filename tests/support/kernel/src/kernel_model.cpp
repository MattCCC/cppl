#include "cppl/testing/kernel_model.hpp"

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::testing::kernel_model {

namespace k = kernel;

namespace {

// Calls nest at most this deep before evaluation gives up. Definitions cannot
// recurse, so only a very long chain of them reaches it.
constexpr std::size_t kMaxDepth = 4096;

bool supported(const k::IntType& type) {
    return type.width >= 1 && type.width <= 64 &&
           (type.signedness == k::Signedness::Signed || type.signedness == k::Signedness::Unsigned);
}

// 2^width, which every supported width keeps well inside a Wide.
Wide modulus(const k::IntType& type) {
    return Wide{1} << type.width;
}

// The low 64 bits of `value`'s two's-complement form.
std::uint64_t low_bits(Wide value) {
    return static_cast<std::uint64_t>(static_cast<k::WideUnsigned>(value));
}

// |value| for any value of a supported type: at most 2^64 - 1.
std::uint64_t magnitude(Wide value) {
    return value < 0 ? low_bits(-value) : low_bits(value);
}

std::uint64_t mix(std::uint64_t state, std::uint64_t value) {
    // SplitMix64's finalizer over the running state: deterministic and
    // platform independent, which is all an interpretation needs.
    state ^= value + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2);
    state ^= state >> 30;
    state *= 0xbf58476d1ce4e5b9ULL;
    state ^= state >> 27;
    state *= 0x94d049bb133111ebULL;
    state ^= state >> 31;
    return state;
}

std::uint64_t name_of(const k::Type& type) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : k::describe(type)) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

bool is_boolean_value(Value value) {
    return value == 0 || value == 1;
}

} // namespace

std::string describe(Truth truth) {
    switch (truth) {
        case Truth::False:
            return "false";
        case Truth::True:
            return "true";
        case Truth::Unknown:
            return "unknown";
    }
    return "invalid";
}

namespace machine {

Wide lowest(const k::IntType& type) {
    return type.signedness == k::Signedness::Signed ? -(modulus(type) / 2) : Wide{0};
}

Wide highest(const k::IntType& type) {
    return type.signedness == k::Signedness::Signed ? modulus(type) / 2 - 1 : modulus(type) - 1;
}

Wide wrap(const k::IntType& type, Wide value) {
    const auto residue =
        static_cast<Wide>(static_cast<k::WideUnsigned>(value) & static_cast<k::WideUnsigned>(modulus(type) - 1));
    return residue > highest(type) ? residue - modulus(type) : residue;
}

Wide add(const k::IntType& type, Wide lhs, Wide rhs) {
    return wrap(type, lhs + rhs);
}

Wide subtract(const k::IntType& type, Wide lhs, Wide rhs) {
    return wrap(type, lhs - rhs);
}

Wide multiply(const k::IntType& type, Wide lhs, Wide rhs) {
    // Unsigned 64-bit multiplication is exact modulo 2^64, and so modulo
    // 2^width for every supported width.
    return wrap(type, static_cast<Wide>(low_bits(lhs) * low_bits(rhs)));
}

Wide quotient(const k::IntType& type, Wide lhs, Wide rhs) {
    if (rhs == 0) {
        return 0;
    }
    const Wide exact{magnitude(lhs) / magnitude(rhs)};
    return wrap(type, (lhs < 0) != (rhs < 0) ? -exact : exact);
}

Wide remainder(Wide lhs, Wide rhs) {
    if (rhs == 0) {
        return lhs;
    }
    const Wide rest{magnitude(lhs) % magnitude(rhs)};
    return lhs < 0 ? -rest : rest;
}

bool fits(const k::IntType& type, k::PrimOp op, Wide lhs, Wide rhs) {
    Wide exact = 0;
    switch (op) {
        case k::PrimOp::AddFits:
            exact = lhs + rhs;
            break;
        case k::PrimOp::SubFits:
            exact = lhs - rhs;
            break;
        case k::PrimOp::MulFits:
            // A product too large for 128 bits is far outside every type.
            if (__builtin_mul_overflow(lhs, rhs, &exact)) {
                return false;
            }
            break;
        default:
            return false;
    }
    return exact >= lowest(type) && exact <= highest(type);
}

} // namespace machine

Evaluator::Evaluator(const k::Context& context, Model model) : context_(context), model_(model) {}

std::optional<Value> Evaluator::value(const k::Term& term, std::span<const Value> environment) {
    return evaluate(term, environment, 0);
}

std::optional<Value> Evaluator::observe(const k::Type& domain, std::uint64_t selector, Value subject,
                                        const k::Type& result) const {
    std::uint64_t hash = mix(model_.seed, name_of(domain));
    hash = mix(hash, selector);
    hash = mix(hash, low_bits(subject));
    if (result.is_integer()) {
        const k::IntType& type = result.integer_type();
        if (!supported(type)) {
            return std::nullopt;
        }
        return machine::wrap(type, Wide{hash});
    }
    if (model_.abstract_carrier == 0) {
        return std::nullopt;
    }
    return Wide{hash % model_.abstract_carrier};
}

std::optional<Value> Evaluator::evaluate(const k::Term& term, std::span<const Value> environment, std::size_t depth) {
    if (++spent_ > model_.budget || depth > kMaxDepth) {
        return std::nullopt;
    }

    return std::visit(
        [&](const auto& node) -> std::optional<Value> {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, k::Var>) {
                if (node.index.value >= environment.size()) {
                    return std::nullopt;
                }
                return environment[environment.size() - 1 - node.index.value];

            } else if constexpr (std::is_same_v<Node, k::Literal>) {
                if (!supported(node.type) || node.value < machine::lowest(node.type) ||
                    node.value > machine::highest(node.type)) {
                    return std::nullopt;
                }
                return node.value;

            } else if constexpr (std::is_same_v<Node, k::Call>) {
                const k::Definition* definition = context_.lookup(node.callee);
                if (definition == nullptr || definition->parameters.size() != node.arguments.size()) {
                    return std::nullopt;
                }
                std::vector<Value> arguments;
                arguments.reserve(node.arguments.size());
                for (const k::Term& argument : node.arguments) {
                    auto evaluated = evaluate(argument, environment, depth + 1);
                    if (!evaluated) {
                        return std::nullopt;
                    }
                    arguments.push_back(*evaluated);
                }
                // Parameters are bound outermost first, exactly as an
                // environment lists binders.
                return evaluate(definition->body, arguments, depth + 1);

            } else if constexpr (std::is_same_v<Node, k::Projection>) {
                if (!node.domain.is_value() || node.arguments.size() != 1) {
                    return std::nullopt;
                }
                const auto& signature = std::get<k::ValueType>(node.domain.node);
                if (node.index >= signature.projections.size()) {
                    return std::nullopt;
                }
                auto subject = evaluate(node.arguments[0], environment, depth + 1);
                if (!subject) {
                    return std::nullopt;
                }
                return observe(node.domain, node.index, *subject, signature.projections[node.index]);

            } else if constexpr (std::is_same_v<Node, k::Element>) {
                if (!node.domain.is_indexed() || node.arguments.size() != 2) {
                    return std::nullopt;
                }
                const auto& indexed = std::get<k::IndexedType>(node.domain.node);
                if (indexed.element.size() != 1) {
                    return std::nullopt;
                }
                auto subject = evaluate(node.arguments[0], environment, depth + 1);
                auto index = evaluate(node.arguments[1], environment, depth + 1);
                if (!subject || !index) {
                    return std::nullopt;
                }
                // An element is a function of the index's value, whatever term
                // denotes it: 128 bits of it are folded into the selector.
                const auto bits = static_cast<k::WideUnsigned>(*index);
                const std::uint64_t selector =
                    mix(static_cast<std::uint64_t>(bits), static_cast<std::uint64_t>(bits >> 64));
                return observe(node.domain, selector, *subject, indexed.element[0]);

            } else {
                static_assert(std::is_same_v<Node, k::Prim>);
                const k::IntType& type = node.type;
                if (!supported(type)) {
                    return std::nullopt;
                }
                std::vector<Value> operands;
                operands.reserve(node.arguments.size());
                for (const k::Term& argument : node.arguments) {
                    auto evaluated = evaluate(argument, environment, depth + 1);
                    if (!evaluated) {
                        return std::nullopt;
                    }
                    operands.push_back(*evaluated);
                }
                const auto binary = [&]() {
                    return operands.size() == 2;
                };
                switch (node.op) {
                    case k::PrimOp::AddWrap:
                        return binary() ? std::optional{machine::add(type, operands[0], operands[1])} : std::nullopt;
                    case k::PrimOp::SubWrap:
                        return binary() ? std::optional{machine::subtract(type, operands[0], operands[1])}
                                        : std::nullopt;
                    case k::PrimOp::MulWrap:
                        return binary() ? std::optional{machine::multiply(type, operands[0], operands[1])}
                                        : std::nullopt;
                    case k::PrimOp::Equal:
                        return binary() ? std::optional{Wide{operands[0] == operands[1] ? 1 : 0}} : std::nullopt;
                    case k::PrimOp::NotEqual:
                        return binary() ? std::optional{Wide{operands[0] != operands[1] ? 1 : 0}} : std::nullopt;
                    case k::PrimOp::Less:
                        return binary() ? std::optional{Wide{operands[0] < operands[1] ? 1 : 0}} : std::nullopt;
                    case k::PrimOp::LessEqual:
                        return binary() ? std::optional{Wide{operands[0] <= operands[1] ? 1 : 0}} : std::nullopt;
                    case k::PrimOp::Greater:
                        return binary() ? std::optional{Wide{operands[0] > operands[1] ? 1 : 0}} : std::nullopt;
                    case k::PrimOp::GreaterEqual:
                        return binary() ? std::optional{Wide{operands[0] >= operands[1] ? 1 : 0}} : std::nullopt;
                    case k::PrimOp::Not:
                        if (operands.size() != 1 || !is_boolean_value(operands[0])) {
                            return std::nullopt;
                        }
                        return Wide{1} - operands[0];
                    case k::PrimOp::Select:
                        if (operands.size() != 3 || !is_boolean_value(operands[0])) {
                            return std::nullopt;
                        }
                        return operands[0] == 1 ? operands[1] : operands[2];
                    case k::PrimOp::AddFits:
                    case k::PrimOp::SubFits:
                    case k::PrimOp::MulFits:
                        return binary() ? std::optional{Wide{machine::fits(type, node.op, operands[0], operands[1])}}
                                        : std::nullopt;
                    case k::PrimOp::Quotient:
                        return binary() ? std::optional{machine::quotient(type, operands[0], operands[1])}
                                        : std::nullopt;
                    case k::PrimOp::Remainder:
                        return binary() ? std::optional{machine::remainder(operands[0], operands[1])} : std::nullopt;
                    case k::PrimOp::Convert:
                        if (operands.size() != 1) {
                            return std::nullopt;
                        }
                        return machine::wrap(type, operands[0]);
                }
                return std::nullopt;
            }
        },
        term.node);
}

std::vector<Value> Evaluator::domain(const k::Type& type, bool& exhaustive) const {
    std::vector<Value> values;
    if (!type.is_integer()) {
        exhaustive = true;
        for (std::uint32_t element = 0; element < model_.abstract_carrier; ++element) {
            values.emplace_back(element);
        }
        return values;
    }
    const k::IntType& integer = type.integer_type();
    if (!supported(integer)) {
        exhaustive = false;
        return values;
    }
    const Wide least = machine::lowest(integer);
    const Wide greatest = machine::highest(integer);
    if (integer.width <= model_.enumerated_width) {
        exhaustive = true;
        for (Wide value = least; value <= greatest; ++value) {
            values.push_back(value);
        }
        return values;
    }
    exhaustive = false;
    for (const Wide candidate : {least, least + 1, Wide{-1}, Wide{0}, Wide{1}, Wide{2}, greatest - 1, greatest}) {
        if (candidate >= least && candidate <= greatest) {
            values.push_back(candidate);
        }
    }
    std::uint64_t state = mix(model_.seed, name_of(type));
    for (int sample = 0; sample < 4; ++sample) {
        state = mix(state, static_cast<std::uint64_t>(sample));
        values.push_back(machine::wrap(integer, Wide{state}));
    }
    std::ranges::sort(values);
    const auto [first, last] = std::ranges::unique(values);
    values.erase(first, last);
    return values;
}

Truth Evaluator::truth(const k::Proposition& proposition, std::span<const Value> environment) {
    if (exhausted()) {
        return Truth::Unknown;
    }

    if (const auto* equality = std::get_if<k::Eq>(&proposition.node)) {
        const auto lhs = value(equality->lhs, environment);
        const auto rhs = value(equality->rhs, environment);
        if (!lhs || !rhs) {
            return Truth::Unknown;
        }
        return *lhs == *rhs ? Truth::True : Truth::False;
    }

    if (const auto* quantified = std::get_if<k::Forall>(&proposition.node)) {
        bool exhaustive = false;
        const std::vector<Value> values = domain(quantified->binder, exhaustive);
        std::vector<Value> extended(environment.begin(), environment.end());
        extended.push_back(0);
        bool certain = exhaustive;
        for (const Value value : values) {
            extended.back() = value;
            const Truth body = truth(*quantified->body, extended);
            if (body == Truth::False) {
                return Truth::False;
            }
            if (body == Truth::Unknown) {
                certain = false;
            }
        }
        return certain ? Truth::True : Truth::Unknown;
    }

    if (const auto* implication = std::get_if<k::Implies>(&proposition.node)) {
        const Truth premise = truth(*implication->premise, environment);
        if (premise == Truth::False) {
            return Truth::True;
        }
        const Truth conclusion = truth(*implication->conclusion, environment);
        if (conclusion == Truth::True) {
            return Truth::True;
        }
        if (premise == Truth::True && conclusion == Truth::False) {
            return Truth::False;
        }
        return Truth::Unknown;
    }

    if (const auto* conjunction = std::get_if<k::And>(&proposition.node)) {
        const Truth left = truth(*conjunction->left, environment);
        if (left == Truth::False) {
            return Truth::False;
        }
        const Truth right = truth(*conjunction->right, environment);
        if (right == Truth::False) {
            return Truth::False;
        }
        return left == Truth::True && right == Truth::True ? Truth::True : Truth::Unknown;
    }

    if (const auto* disjunction = std::get_if<k::Or>(&proposition.node)) {
        const Truth left = truth(*disjunction->left, environment);
        if (left == Truth::True) {
            return Truth::True;
        }
        const Truth right = truth(*disjunction->right, environment);
        if (right == Truth::True) {
            return Truth::True;
        }
        return left == Truth::False && right == Truth::False ? Truth::False : Truth::Unknown;
    }

    static_assert(std::variant_size_v<decltype(k::Proposition::node)> == 6,
                  "a proposition former was added: give it a meaning in the kernel model");
    if (std::holds_alternative<k::Falsity>(proposition.node)) {
        return Truth::False;
    }
    return Truth::Unknown;
}

} // namespace cppl::testing::kernel_model
