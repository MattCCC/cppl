#include "cppl/vir/capability.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/types.hpp"

#include <variant>

namespace cppl::vir {

std::string describe(const Type& type) {
    if (type.is_void())
        return "void";
    // A value that stands for a C++ representation is named the way it was
    // written, not by the scalar it is carried in: a diagnostic about a scoped
    // enum should say `State`, not `i32`.
    if (type.representation.is_known() && !type.representation.name.empty()) {
        return type.representation.name;
    }
    if (type.is_value())
        return "abstract value";
    if (type.is_proposition())
        return "Prop";
    if (type.is_boolean()) {
        return "bool";
    }
    const IntType& integer = type.integer_type();
    return (integer.is_signed ? "i" : "u") + std::to_string(integer.width);
}

// How a place is written, for diagnostics. The spelling the bridge recorded is
// what the author wrote, so it is preferred; the structural form is the
// fallback when a place was built without one.
std::string describe(const Place& place) {
    if (!place.spelling.empty()) {
        return place.spelling;
    }
    std::string text;
    switch (place.root.kind) {
        case PlaceRoot::Kind::Parameter:
            text = "parameter#" + std::to_string(place.root.id);
            break;
        case PlaceRoot::Kind::Deref:
            text = "*(#" + std::to_string(place.root.id) + "@" + std::to_string(place.root.version) + ")";
            break;
        case PlaceRoot::Kind::Local:
            text = "local#" + std::to_string(place.root.id);
            break;
    }
    for (const PlaceStep& step : place.path) {
        switch (step.kind) {
            case PlaceStep::Kind::Field:
                text += ".field" + std::to_string(step.index);
                break;
            case PlaceStep::Kind::Element:
                text += "[" + std::to_string(step.index) + "]";
                break;
            case PlaceStep::Kind::SymbolicElement:
                text += "[#" + std::to_string(step.symbol) + "]";
                break;
        }
    }
    return text;
}

std::string describe(CapabilityKind kind) {
    return kind == CapabilityKind::Readable ? "readable" : "writable";
}

std::string describe(const Capability& capability) {
    std::string text = describe(capability.kind) + "(" + describe(capability.place);
    if (!capability.extent.empty()) {
        text += ", " + describe(capability.extent.front());
    }
    return text + ")";
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
        case BinaryOp::And:
            return "&&";
        case BinaryOp::Or:
            return "||";
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
            } else if constexpr (std::is_same_v<Node, Universal>) {
                return node.body.size() == 1
                           ? "forall(" + std::to_string(node.binders.size()) + "). " + describe(node.body[0])
                           : "<malformed-universal>";
            } else if constexpr (std::is_same_v<Node, Implication>) {
                return node.operands.size() == 2
                           ? "(" + describe(node.operands[0]) + " -> " + describe(node.operands[1]) + ")"
                           : "<malformed-implication>";
            } else if constexpr (std::is_same_v<Node, Connective>) {
                const auto op = node.kind == Connective::Kind::Conjunction   ? " && "
                                : node.kind == Connective::Kind::Disjunction ? " || "
                                                                             : " <-> ";
                return node.operands.size() == 2
                           ? "(" + describe(node.operands[0]) + op + describe(node.operands[1]) + ")"
                           : "<malformed-connective>";
            } else if constexpr (std::is_same_v<Node, Projection>) {
                return node.operands.size() == 1
                           ? "project<" + std::to_string(node.index) + ">(" + describe(node.operands[0]) + ")"
                           : "<malformed-projection>";
            } else if constexpr (std::is_same_v<Node, FormalEquality>) {
                return node.operands.size() == 2
                           ? "Eq<" + describe(node.operand_type) + ">(" + describe(node.operands[0]) + ", " +
                                 describe(node.operands[1]) + ")"
                           : "<malformed-equality>";
            } else if constexpr (std::is_same_v<Node, Negation>) {
                return node.operands.size() == 1 ? "!(" + describe(node.operands[0]) + ")" : "<malformed-negation>";
            } else if constexpr (std::is_same_v<Node, Conditional>) {
                return node.operands.size() == 3
                           ? "if(" + describe(node.operands[0]) + ", " + describe(node.operands[1]) + ", " +
                                 describe(node.operands[2]) + ")"
                           : "<malformed-conditional>";
            } else if constexpr (std::is_same_v<Node, PlaceVersion>) {
                return node.operands.size() == 2 ? "let " + describe(node.place) + "#" + std::to_string(node.version) +
                                                       " = " + describe(node.operands[0]) + " in " +
                                                       describe(node.operands[1])
                                                 : "<malformed-place>";
            } else if constexpr (std::is_same_v<Node, PlaceRef>) {
                return describe(node.place) + "#" + std::to_string(node.version);
            } else if constexpr (std::is_same_v<Node, Loop>) {
                std::string text = "loop#" + std::to_string(node.loop) + "(";
                for (std::size_t index = 0; index < node.operands.size(); ++index) {
                    text += (index != 0 ? ", " : "") + describe(node.operands[index]);
                }
                return text + ")";
            } else if constexpr (std::is_same_v<Node, ReturnState>) {
                std::string text = "complete(";
                for (const auto& operand : node.operands)
                    text += describe(operand) + "; ";
                return text + ")";
            } else if constexpr (std::is_same_v<Node, UnknownVersion>) {
                return node.operands.size() == 1 ? "havoc " + describe(node.place) + "#" +
                                                       std::to_string(node.version) + " in " +
                                                       describe(node.operands.front())
                                                 : "<malformed-mutation>";
            } else if constexpr (std::is_same_v<Node, ElementBound>) {
                return node.operands.size() == 2 && node.extent.size() == 1
                           ? "bounded(" + describe(node.operands[0]) + " < " + describe(node.extent.front()) + ") in " +
                                 describe(node.operands[1])
                           : "<malformed-bound>";
            } else if constexpr (std::is_same_v<Node, Iterate>) {
                std::string text = "next#" + std::to_string(node.loop) + "(";
                for (std::size_t index = 0; index < node.operands.size(); ++index) {
                    text += (index != 0 ? ", " : "") + describe(node.operands[index]);
                }
                return text + ")";
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

const RefinementDeclaration* Module::find_refinement(std::string_view name) const {
    for (const RefinementDeclaration& refinement : refinements) {
        if (refinement.name == name) {
            return &refinement;
        }
    }
    return nullptr;
}

} // namespace cppl::vir
