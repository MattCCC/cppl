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
        case BinaryOp::Sub:
            return "-";
        case BinaryOp::Mul:
            return "*";
        case BinaryOp::Equal:
            return "==";
        case BinaryOp::NotEqual:
            return "!=";
        case BinaryOp::Less:
            return "<";
        case BinaryOp::LessEqual:
            return "<=";
        case BinaryOp::Greater:
            return ">";
        case BinaryOp::GreaterEqual:
            return ">=";
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
            } else if constexpr (std::is_same_v<Node, Negation>) {
                return node.operands.size() == 1 ? "!(" + describe(node.operands[0]) + ")" : "<malformed-negation>";
            } else if constexpr (std::is_same_v<Node, Conditional>) {
                return node.operands.size() == 3
                           ? "if(" + describe(node.operands[0]) + ", " + describe(node.operands[1]) + ", " +
                                 describe(node.operands[2]) + ")"
                           : "<malformed-conditional>";
            } else if constexpr (std::is_same_v<Node, LocalVersion>) {
                return node.operands.size() == 2 ? "let " + node.name + "#" + std::to_string(node.version) + " = " +
                                                       describe(node.operands[0]) + " in " + describe(node.operands[1])
                                                 : "<malformed-local>";
            } else if constexpr (std::is_same_v<Node, LocalRef>) {
                return node.name + "#" + std::to_string(node.version);
            } else {
                if (node.operands.size() != 2) {
                    return "<malformed-binary>";
                }
                return "(" + describe(node.operands[0]) + " " + describe(node.op) + " " + describe(node.operands[1]) +
                       ")";
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

} // namespace cppl::vir
