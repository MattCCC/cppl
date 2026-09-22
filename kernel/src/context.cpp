#include "cppl/kernel/context.hpp"

#include "cppl/kernel/arithmetic.hpp"

#include <utility>
#include <variant>

namespace cppl::kernel {

namespace {

std::unexpected<CoreError> fail(CoreErrorKind kind, std::string detail) {
    return std::unexpected(CoreError{kind, std::move(detail)});
}

[[nodiscard]] std::expected<void, CoreError> validate_type(const Type& type) {
    if (!is_supported(type)) {
        return fail(CoreErrorKind::MalformedType, "unsupported or malformed core type");
    }
    return {};
}

[[nodiscard]] std::expected<Type, CoreError> type_of_impl(const Context& context, std::span<const Type> locals,
                                                          const Term& term, const CoreLimits& limits,
                                                          std::uint32_t depth) {
    if (depth > limits.max_term_depth) {
        return fail(CoreErrorKind::DepthLimitExceeded, "term nests deeper than the core allows");
    }

    return std::visit(
        [&](const auto& node) -> std::expected<Type, CoreError> {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, Var>) {
                if (node.index.value >= locals.size()) {
                    return fail(CoreErrorKind::VariableOutOfScope,
                                "variable #" + std::to_string(node.index.value) + " has no enclosing binder");
                }
                const Type& type = locals[locals.size() - 1 - node.index.value];
                if (auto valid = validate_type(type); !valid) {
                    return std::unexpected(valid.error());
                }
                return type;

            } else if constexpr (std::is_same_v<Node, Literal>) {
                Type type{node.type};
                if (auto valid = validate_type(type); !valid) {
                    return std::unexpected(valid.error());
                }
                if (!is_representable(node.type, node.value)) {
                    return fail(CoreErrorKind::MalformedLiteral, "literal " + describe(node.value) +
                                                                     " is not representable in " + describe(node.type));
                }
                return std::move(type);

            } else if constexpr (std::is_same_v<Node, Call>) {
                const Definition* definition = context.lookup(node.callee);
                if (definition == nullptr) {
                    return fail(CoreErrorKind::UnknownDefinition,
                                "def#" + std::to_string(node.callee.value) + " is not defined");
                }
                if (definition->parameters.size() != node.arguments.size()) {
                    return fail(CoreErrorKind::ArityMismatch, "def#" + std::to_string(node.callee.value) + " expects " +
                                                                  std::to_string(definition->parameters.size()) +
                                                                  " arguments but received " +
                                                                  std::to_string(node.arguments.size()));
                }
                for (std::size_t index = 0; index < node.arguments.size(); ++index) {
                    auto argument = type_of_impl(context, locals, node.arguments[index], limits, depth + 1);
                    if (!argument) {
                        return argument;
                    }
                    if (!(*argument == definition->parameters[index])) {
                        return fail(CoreErrorKind::TypeMismatch,
                                    "argument " + std::to_string(index) + " of def#" +
                                        std::to_string(node.callee.value) + " has type " + describe(*argument) +
                                        " but " + describe(definition->parameters[index]) + " is required");
                    }
                }
                return definition->result;

            } else if constexpr (std::is_same_v<Node, Projection>) {
                if (!is_supported(node.domain) || !node.domain.is_value())
                    return fail(CoreErrorKind::MalformedType, "projection requires a valid abstract domain");
                const auto& signature = std::get<ValueType>(node.domain.node);
                if (node.index >= signature.projections.size())
                    return fail(CoreErrorKind::MalformedPrimitive, "projection index is outside its signature");
                if (node.arguments.size() != 1)
                    return fail(CoreErrorKind::ArityMismatch, "projection requires exactly one subject");
                auto subject = type_of_impl(context, locals, node.arguments[0], limits, depth + 1);
                if (!subject)
                    return subject;
                if (*subject != node.domain)
                    return fail(CoreErrorKind::TypeMismatch, "projection subject does not match its domain signature");
                return signature.projections[node.index];
            } else {
                const Type type{node.type};
                if (auto valid = validate_type(type); !valid) {
                    return std::unexpected(valid.error());
                }
                if (!is_arithmetic(node.op) && !is_comparison(node.op) && node.op != PrimOp::Not &&
                    node.op != PrimOp::Select) {
                    return fail(CoreErrorKind::MalformedPrimitive, "unrecognized primitive");
                }
                if (node.op == PrimOp::Not && !(node.type == kBoolean)) {
                    return fail(CoreErrorKind::TypeMismatch, "negation requires a boolean");
                }
                const std::size_t required_arity = node.op == PrimOp::Select ? 3u : node.op == PrimOp::Not ? 1u : 2u;
                if (node.arguments.size() != required_arity) {
                    return fail(CoreErrorKind::ArityMismatch,
                                describe(node.op) + " expects " + std::to_string(required_arity) +
                                    " arguments but received " + std::to_string(node.arguments.size()));
                }
                for (std::size_t index = 0; index < node.arguments.size(); ++index) {
                    const auto& argument = node.arguments[index];
                    const Type expected = node.op == PrimOp::Select && index == 0 ? Type{kBoolean} : type;
                    auto argument_type = type_of_impl(context, locals, argument, limits, depth + 1);
                    if (!argument_type) {
                        return argument_type;
                    }
                    if (!(*argument_type == expected)) {
                        return fail(CoreErrorKind::TypeMismatch, describe(node.op) + " operates on " +
                                                                     describe(expected) + " but received " +
                                                                     describe(*argument_type));
                    }
                }
                return is_comparison(node.op) ? Type{kBoolean} : type;
            }
        },
        term.node);
}

[[nodiscard]] std::expected<Term, CoreError> substitute(const Term& term, const std::vector<Term>& arguments,
                                                        const CoreLimits& limits, std::uint32_t depth) {
    if (depth > limits.max_term_depth) {
        return fail(CoreErrorKind::DepthLimitExceeded, "substitution nests deeper than the core allows");
    }

    return std::visit(
        [&](const auto& node) -> std::expected<Term, CoreError> {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, Var>) {
                if (node.index.value >= arguments.size()) {
                    return fail(CoreErrorKind::VariableOutOfScope, "definition body references variable #" +
                                                                       std::to_string(node.index.value) +
                                                                       " which is not one of its parameters");
                }
                return arguments[arguments.size() - 1 - node.index.value];

            } else if constexpr (std::is_same_v<Node, Literal>) {
                return Term{node};

            } else {
                Node replaced = node;
                replaced.arguments.clear();
                replaced.arguments.reserve(node.arguments.size());
                for (const Term& argument : node.arguments) {
                    auto substituted = substitute(argument, arguments, limits, depth + 1);
                    if (!substituted) {
                        return substituted;
                    }
                    replaced.arguments.push_back(std::move(*substituted));
                }
                return Term{std::move(replaced)};
            }
        },
        term.node);
}

[[nodiscard]] std::expected<Term, CoreError> normalize_impl(const Context& context, const Term& term,
                                                            const CoreLimits& limits, std::uint64_t& steps,
                                                            std::uint32_t depth) {
    if (depth > limits.max_term_depth) {
        return fail(CoreErrorKind::DepthLimitExceeded, "normalization nests deeper than the core allows");
    }
    if (steps > limits.max_normalization_steps) {
        return fail(CoreErrorKind::NormalizationBudgetExhausted, "normalization exceeded its step budget");
    }

    return std::visit(
        [&](const auto& node) -> std::expected<Term, CoreError> {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, Var> || std::is_same_v<Node, Literal>) {
                return Term{node};

            } else if constexpr (std::is_same_v<Node, Call>) {
                const Definition* definition = context.lookup(node.callee);
                if (definition == nullptr) {
                    return fail(CoreErrorKind::UnknownDefinition,
                                "def#" + std::to_string(node.callee.value) + " is not defined");
                }
                if (definition->parameters.size() != node.arguments.size()) {
                    return fail(CoreErrorKind::ArityMismatch, "def#" + std::to_string(node.callee.value) +
                                                                  " applied with the wrong number of arguments");
                }

                std::vector<Term> arguments;
                arguments.reserve(node.arguments.size());
                for (const Term& argument : node.arguments) {
                    auto normalized = normalize_impl(context, argument, limits, steps, depth + 1);
                    if (!normalized) {
                        return normalized;
                    }
                    arguments.push_back(std::move(*normalized));
                }

                ++steps;
                auto unfolded = substitute(definition->body, arguments, limits, depth + 1);
                if (!unfolded) {
                    return unfolded;
                }
                return normalize_impl(context, *unfolded, limits, steps, depth + 1);

            } else {
                std::vector<Term> arguments;
                arguments.reserve(node.arguments.size());
                for (const Term& argument : node.arguments) {
                    auto normalized = normalize_impl(context, argument, limits, steps, depth + 1);
                    if (!normalized) {
                        return normalized;
                    }
                    arguments.push_back(std::move(*normalized));
                }

                ++steps;
                if constexpr (std::is_same_v<Node, Projection>) {
                    if (!is_supported(node.domain) || !node.domain.is_value() || arguments.size() != 1 ||
                        node.index >= std::get<ValueType>(node.domain.node).projections.size())
                        return fail(CoreErrorKind::MalformedPrimitive, "malformed projection");
                    return Term{Projection{node.domain, node.index, std::move(arguments)}};
                } else {
                    return normalize_primitive(node.op, node.type, std::move(arguments), limits, steps);
                }
            }
        },
        term.node);
}

std::string describe_with_names(const Context& context, const Term& term) {
    return std::visit(
        [&](const auto& node) -> std::string {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, Var>) {
                return "#" + std::to_string(node.index.value);

            } else if constexpr (std::is_same_v<Node, Literal>) {
                return describe(node.value);

            } else {
                std::string text;
                if constexpr (std::is_same_v<Node, Call>) {
                    const Definition* definition = context.lookup(node.callee);
                    text = definition != nullptr ? definition->name : "def#" + std::to_string(node.callee.value);
                } else if constexpr (std::is_same_v<Node, Projection>) {
                    text = "project[" + describe(node.domain) + "," + std::to_string(node.index) + "]";
                } else {
                    text = describe(node.op);
                }
                text += "(";
                for (std::size_t index = 0; index < node.arguments.size(); ++index) {
                    if (index != 0) {
                        text += ", ";
                    }
                    text += describe_with_names(context, node.arguments[index]);
                }
                text += ")";
                return text;
            }
        },
        term.node);
}

} // namespace

std::string describe(CoreErrorKind kind) {
    switch (kind) {
        case CoreErrorKind::DuplicateDefinition:
            return "duplicate definition";
        case CoreErrorKind::UnknownDefinition:
            return "unknown definition";
        case CoreErrorKind::ArityMismatch:
            return "arity mismatch";
        case CoreErrorKind::TypeMismatch:
            return "type mismatch";
        case CoreErrorKind::VariableOutOfScope:
            return "variable out of scope";
        case CoreErrorKind::MalformedLiteral:
            return "malformed literal";
        case CoreErrorKind::MalformedPrimitive:
            return "malformed primitive";
        case CoreErrorKind::MalformedType:
            return "malformed type";
        case CoreErrorKind::DepthLimitExceeded:
            return "depth limit exceeded";
        case CoreErrorKind::NormalizationBudgetExhausted:
            return "normalization budget exhausted";
    }
    return "unknown core error";
}

std::expected<void, CoreError> Context::define(Definition definition) {
    if (lookup(definition.id) != nullptr) {
        return fail(CoreErrorKind::DuplicateDefinition,
                    "def#" + std::to_string(definition.id.value) + " is already defined");
    }

    for (const Type& parameter : definition.parameters) {
        if (auto valid = validate_type(parameter); !valid) {
            return valid;
        }
    }
    if (auto valid = validate_type(definition.result); !valid) {
        return valid;
    }

    // The body is checked against the context as it stands, which excludes the
    // definition being added. A definition therefore cannot call itself or any
    // later definition, and the call graph stays acyclic.
    const CoreLimits limits{};
    auto body_type = type_of_impl(*this, definition.parameters, definition.body, limits, 0);
    if (!body_type) {
        return std::unexpected(body_type.error());
    }
    if (!(*body_type == definition.result)) {
        return fail(CoreErrorKind::TypeMismatch, "definition '" + definition.name + "' returns " +
                                                     describe(*body_type) + " but declares " +
                                                     describe(definition.result));
    }

    definitions_.push_back(std::move(definition));
    return {};
}

const Definition* Context::lookup(DefId id) const noexcept {
    for (const Definition& definition : definitions_) {
        if (definition.id == id) {
            return &definition;
        }
    }
    return nullptr;
}

std::expected<Type, CoreError> type_of(const Context& context, std::span<const Type> locals, const Term& term,
                                       const CoreLimits& limits) {
    return type_of_impl(context, locals, term, limits, 0);
}

std::expected<Term, CoreError> normalize(const Context& context, const Term& term, const CoreLimits& limits) {
    std::uint64_t steps = 0;
    return normalize_impl(context, term, limits, steps, 0);
}

std::string describe(const Context& context, const Term& term) {
    return describe_with_names(context, term);
}

} // namespace cppl::kernel
