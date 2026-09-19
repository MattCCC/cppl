#include "cppl/kernel/context.hpp"

#include <algorithm>
#include <utility>
#include <variant>

namespace cppl::kernel {

namespace {

std::unexpected<CoreError> fail(CoreErrorKind kind, std::string detail) {
    return std::unexpected(CoreError{kind, std::move(detail)});
}

[[nodiscard]] std::expected<void, CoreError> validate_type(const Type& type) {
    if (!type.is_integer()) {
        return fail(CoreErrorKind::MalformedType, "type is not a recognized core type");
    }
    if (!is_supported(type.integer_type())) {
        return fail(CoreErrorKind::MalformedType,
                    "integer width " + std::to_string(type.integer_type().width) +
                        " is outside the range this core supports");
    }
    return {};
}

[[nodiscard]] std::expected<Type, CoreError> type_of_impl(const Context& context,
                                                          std::span<const Type> locals,
                                                          const Term& term,
                                                          const CoreLimits& limits,
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
                                "variable #" + std::to_string(node.index.value) +
                                    " has no enclosing binder");
                }
                return locals[locals.size() - 1 - node.index.value];

            } else if constexpr (std::is_same_v<Node, Literal>) {
                const Type type{node.type};
                if (auto valid = validate_type(type); !valid) {
                    return std::unexpected(valid.error());
                }
                if (!is_representable(node.type, node.value)) {
                    return fail(CoreErrorKind::MalformedLiteral,
                                "literal " + std::to_string(node.value) +
                                    " is not representable in " + describe(node.type));
                }
                return type;

            } else if constexpr (std::is_same_v<Node, Call>) {
                const Definition* definition = context.lookup(node.callee);
                if (definition == nullptr) {
                    return fail(CoreErrorKind::UnknownDefinition,
                                "def#" + std::to_string(node.callee.value) + " is not defined");
                }
                if (definition->parameters.size() != node.arguments.size()) {
                    return fail(CoreErrorKind::ArityMismatch,
                                "def#" + std::to_string(node.callee.value) + " expects " +
                                    std::to_string(definition->parameters.size()) +
                                    " arguments but received " +
                                    std::to_string(node.arguments.size()));
                }
                for (std::size_t index = 0; index < node.arguments.size(); ++index) {
                    auto argument = type_of_impl(context, locals, node.arguments[index], limits,
                                                 depth + 1);
                    if (!argument) {
                        return argument;
                    }
                    if (!(*argument == definition->parameters[index])) {
                        return fail(CoreErrorKind::TypeMismatch,
                                    "argument " + std::to_string(index) + " of def#" +
                                        std::to_string(node.callee.value) + " has type " +
                                        describe(*argument) + " but " +
                                        describe(definition->parameters[index]) + " is required");
                    }
                }
                return definition->result;

            } else {
                const Type type{node.type};
                if (auto valid = validate_type(type); !valid) {
                    return std::unexpected(valid.error());
                }
                const std::size_t required_arity = node.op == PrimOp::AddWrap ? 2u : 0u;
                if (node.arguments.size() != required_arity) {
                    return fail(CoreErrorKind::ArityMismatch,
                                describe(node.op) + " expects " + std::to_string(required_arity) +
                                    " arguments but received " +
                                    std::to_string(node.arguments.size()));
                }
                for (const Term& argument : node.arguments) {
                    auto argument_type = type_of_impl(context, locals, argument, limits, depth + 1);
                    if (!argument_type) {
                        return argument_type;
                    }
                    if (!(*argument_type == type)) {
                        return fail(CoreErrorKind::TypeMismatch,
                                    describe(node.op) + " operates on " + describe(type) +
                                        " but received " + describe(*argument_type));
                    }
                }
                return type;
            }
        },
        term.node);
}

[[nodiscard]] std::expected<Term, CoreError> substitute(const Term& term,
                                                        const std::vector<Term>& arguments,
                                                        const CoreLimits& limits,
                                                        std::uint32_t depth) {
    if (depth > limits.max_term_depth) {
        return fail(CoreErrorKind::DepthLimitExceeded,
                    "substitution nests deeper than the core allows");
    }

    return std::visit(
        [&](const auto& node) -> std::expected<Term, CoreError> {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, Var>) {
                if (node.index.value >= arguments.size()) {
                    return fail(CoreErrorKind::VariableOutOfScope,
                                "definition body references variable #" +
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

[[nodiscard]] std::expected<Term, CoreError> normalize_impl(const Context& context,
                                                            const Term& term,
                                                            const CoreLimits& limits,
                                                            std::uint64_t& steps,
                                                            std::uint32_t depth) {
    if (depth > limits.max_term_depth) {
        return fail(CoreErrorKind::DepthLimitExceeded,
                    "normalization nests deeper than the core allows");
    }
    if (steps > limits.max_normalization_steps) {
        return fail(CoreErrorKind::NormalizationBudgetExhausted,
                    "normalization exceeded its step budget");
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
                    return fail(CoreErrorKind::ArityMismatch,
                                "def#" + std::to_string(node.callee.value) +
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

                const bool all_literal =
                    std::all_of(arguments.begin(), arguments.end(), [](const Term& argument) {
                        return std::holds_alternative<Literal>(argument.node);
                    });

                if (all_literal && node.op == PrimOp::AddWrap && arguments.size() == 2) {
                    ++steps;
                    const auto& lhs = std::get<Literal>(arguments[0].node);
                    const auto& rhs = std::get<Literal>(arguments[1].node);
                    const std::uint64_t sum =
                        static_cast<std::uint64_t>(lhs.value) + static_cast<std::uint64_t>(rhs.value);
                    return Term::literal(node.type,
                                         wrap_into(node.type, static_cast<std::int64_t>(sum)));
                }

                return Term::primitive(node.op, node.type, std::move(arguments));
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
                return std::to_string(node.value);

            } else {
                std::string text;
                if constexpr (std::is_same_v<Node, Call>) {
                    const Definition* definition = context.lookup(node.callee);
                    text = definition != nullptr ? definition->name
                                                 : "def#" + std::to_string(node.callee.value);
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

}  // namespace

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
        return fail(CoreErrorKind::TypeMismatch,
                    "definition '" + definition.name + "' returns " + describe(*body_type) +
                        " but declares " + describe(definition.result));
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

std::expected<Type, CoreError> type_of(const Context& context,
                                       std::span<const Type> locals,
                                       const Term& term,
                                       const CoreLimits& limits) {
    return type_of_impl(context, locals, term, limits, 0);
}

std::expected<Term, CoreError> normalize(const Context& context,
                                         const Term& term,
                                         const CoreLimits& limits) {
    std::uint64_t steps = 0;
    return normalize_impl(context, term, limits, steps, 0);
}

std::string describe(const Context& context, const Term& term) {
    return describe_with_names(context, term);
}

}  // namespace cppl::kernel
