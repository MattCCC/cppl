#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/types.hpp"

#include <variant>

namespace cppl::vir {

std::string describe(const Type& type) {
    if (type.is_boolean()) {
        return "bool";
    }
    const IntType& integer = type.integer_type();
    return (integer.is_signed ? "i" : "u") + std::to_string(integer.width);
}

std::string describe(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add:
            return "+";
        case BinaryOp::Equal:
            return "==";
    }
    return "<unknown-operator>";
}

std::string describe(const Expr& expr) {
    return std::visit(
        [](const auto& node) -> std::string {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, ParameterRef>) {
                return node.name;
            } else if constexpr (std::is_same_v<Node, IntLiteral>) {
                return std::to_string(node.value);
            } else if constexpr (std::is_same_v<Node, Call>) {
                std::string text = node.callee_name + "(";
                for (std::size_t index = 0; index < node.arguments.size(); ++index) {
                    if (index != 0) {
                        text += ", ";
                    }
                    text += describe(node.arguments[index]);
                }
                text += ")";
                return text;
            } else {
                if (node.operands.size() != 2) {
                    return "<malformed-binary>";
                }
                return "(" + describe(node.operands[0]) + " " + describe(node.op) + " " +
                       describe(node.operands[1]) + ")";
            }
        },
        expr.node);
}

const Function* Module::find(const SymbolId& symbol) const {
    for (const Function& function : functions) {
        if (function.symbol == symbol) {
            return &function;
        }
    }
    return nullptr;
}

}  // namespace cppl::vir
