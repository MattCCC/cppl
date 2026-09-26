#include "cppl/kernel/term.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace cppl::kernel {

std::string describe(PrimOp op) {
    switch (op) {
        case PrimOp::AddWrap:
            return "add_wrap";
        case PrimOp::Equal:
            return "eq";
        case PrimOp::NotEqual:
            return "ne";
        case PrimOp::Less:
            return "lt";
        case PrimOp::LessEqual:
            return "le";
        case PrimOp::Greater:
            return "gt";
        case PrimOp::GreaterEqual:
            return "ge";
        case PrimOp::Not:
            return "not";
        case PrimOp::Select:
            return "select";
        case PrimOp::SubWrap:
            return "sub_wrap";
        case PrimOp::MulWrap:
            return "mul_wrap";
        case PrimOp::AddFits:
            return "add_fits";
        case PrimOp::SubFits:
            return "sub_fits";
        case PrimOp::MulFits:
            return "mul_fits";
        case PrimOp::Quotient:
            return "quot";
        case PrimOp::Remainder:
            return "rem";
        case PrimOp::Convert:
            return "convert";
    }
    return "<unknown-primitive>";
}

bool is_arithmetic(PrimOp op) {
    return op == PrimOp::AddWrap || op == PrimOp::SubWrap || op == PrimOp::MulWrap;
}

bool is_representability(PrimOp op) {
    return op == PrimOp::AddFits || op == PrimOp::SubFits || op == PrimOp::MulFits;
}

bool yields_boolean(PrimOp op) {
    return is_comparison(op) || is_representability(op) || op == PrimOp::Not;
}

std::size_t arity(PrimOp op) {
    switch (op) {
        case PrimOp::Select:
            return 3;
        case PrimOp::Not:
        case PrimOp::Convert:
            return 1;
        case PrimOp::AddWrap:
        case PrimOp::SubWrap:
        case PrimOp::MulWrap:
        case PrimOp::Equal:
        case PrimOp::NotEqual:
        case PrimOp::Less:
        case PrimOp::LessEqual:
        case PrimOp::Greater:
        case PrimOp::GreaterEqual:
        case PrimOp::AddFits:
        case PrimOp::SubFits:
        case PrimOp::MulFits:
        case PrimOp::Quotient:
        case PrimOp::Remainder:
            return 2;
    }
    return 0;
}

bool is_comparison(PrimOp op) {
    switch (op) {
        case PrimOp::Equal:
        case PrimOp::NotEqual:
        case PrimOp::Less:
        case PrimOp::LessEqual:
        case PrimOp::Greater:
        case PrimOp::GreaterEqual:
            return true;
        default:
            return false;
    }
}

VarIndex parameter_reference(std::size_t parameter_count, std::size_t position) {
    if (position >= parameter_count) {
        // Out-of-range parameter references are rejected by type checking; the
        // value returned here is deliberately invalid rather than clamped.
        return VarIndex{static_cast<std::uint32_t>(parameter_count)};
    }
    return VarIndex{static_cast<std::uint32_t>(parameter_count - 1 - position)};
}

namespace {

std::string describe_arguments(const std::vector<Term>& arguments) {
    std::string text = "(";
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index != 0) {
            text += ", ";
        }
        text += describe(arguments[index]);
    }
    text += ")";
    return text;
}

} // namespace

std::string describe(const Term& term) {
    return std::visit(
        [](const auto& node) -> std::string {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, Var>) {
                return "#" + std::to_string(node.index.value);
            } else if constexpr (std::is_same_v<Node, Literal>) {
                return describe(node.value) + ":" + describe(node.type);
            } else if constexpr (std::is_same_v<Node, Call>) {
                return "def#" + std::to_string(node.callee.value) + describe_arguments(node.arguments);
            } else if constexpr (std::is_same_v<Node, Projection>) {
                return "project[" + describe(node.domain) + "," + std::to_string(node.index) + "]" +
                       describe_arguments(node.arguments);
            } else if constexpr (std::is_same_v<Node, Element>) {
                return "element[" + describe(node.domain) + "]" + describe_arguments(node.arguments);
            } else {
                return describe(node.op) + ":" + describe(node.type) + describe_arguments(node.arguments);
            }
        },
        term.node);
}

} // namespace cppl::kernel
