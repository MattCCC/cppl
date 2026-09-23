#include "cppl/decomposition/decomposition.hpp"
#include "cppl/decomposition/providers.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::decomposition {
namespace {
using K = source::RepresentationKind;

vir::Expr projection(const Subject& subject, std::uint32_t index) {
    vir::Expr result;
    result.type = std::get<vir::ValueType>(subject.expression.type.node).projections[index];
    result.provenance = subject.expression.provenance;
    result.node = vir::Projection{index, {subject.expression}};
    return result;
}

ProofBinding binding(const Subject& subject, std::uint32_t index, std::string name) {
    auto value = projection(subject, index);
    return {std::move(name), value.type, BindingKind::Projection, std::move(value)};
}

class StructuralProvider final : public Provider {
  public:
    std::string_view name() const override {
        return "resolved structural values";
    }
    bool recognizes(const vir::Type& type) const override {
        return type.is_value() && type.representation.kind != K::None;
    }
    Decomposition decompose(const Subject& subject) const override {
        const auto& type = subject.expression.type;
        const auto& model = type.representation;
        const auto& signature = std::get<vir::ValueType>(type.node).projections;
        if (!model.rejection.empty())
            return Unsupported{describe(type), model.rejection};
        if (model.kind == K::Record || model.kind == K::Pair || model.kind == K::Tuple || model.kind == K::Array ||
            model.kind == K::StdArray) {
            if (model.components.size() != signature.size())
                return Unsupported{describe(type), "malformed component signature"};
            ProductDecomposition product;
            for (std::size_t i = 0; i < signature.size(); ++i) {
                if (!model.components[i].accessible)
                    return Unsupported{describe(type),
                                       "product decomposition cannot access member '" + model.components[i].name + "'"};
                product.fields.push_back(binding(subject, static_cast<std::uint32_t>(i), model.components[i].name));
            }
            return product;
        }
        if (signature.empty())
            return Unsupported{describe(type), "missing state observation"};
        SumDecomposition sum;
        if (model.kind == K::Variant) {
            for (std::size_t i = 1; i < signature.size(); ++i) {
                auto tag = projection(subject, 0);
                vir::Expr index;
                index.type = tag.type;
                index.provenance = subject.expression.provenance;
                index.node = vir::IntLiteral{static_cast<std::int64_t>(i - 1)};
                vir::Expr condition;
                condition.type = vir::Type::boolean();
                condition.provenance = subject.expression.provenance;
                condition.node = vir::Binary{vir::BinaryOp::Equal, {std::move(tag), std::move(index)}};
                sum.cases.push_back({{"alternative<" + std::to_string(i - 1) + ">", false},
                                     std::move(condition),
                                     {binding(subject, static_cast<std::uint32_t>(i), "value")}});
            }
            sum.residual = {"valueless", false};
        } else if (model.kind == K::Optional || model.kind == K::Expected) {
            if (signature.size() != (model.kind == K::Optional ? 2u : 3u))
                return Unsupported{describe(type), "malformed tagged payload signature"};
            std::vector<ProofBinding> payload;
            if (signature[1].representation.identity != "unit")
                payload.push_back(binding(subject, 1, "value"));
            sum.cases.push_back(
                {{model.kind == K::Optional ? "some" : "value", false}, projection(subject, 0), std::move(payload)});
            sum.residual = {model.kind == K::Optional ? "none" : "error", false};
            if (model.kind == K::Expected)
                sum.residual_bindings.push_back(binding(subject, 2, "error"));
        } else if (model.kind == K::Pointer) {
            sum.cases.push_back({{"null", false}, projection(subject, 0), {}});
            sum.residual = {"non_null", false};
        } else {
            return Unsupported{describe(type), "resolved representation has no structural state model"};
        }
        return sum;
    }
    std::optional<std::size_t> resolve_label(const SumDecomposition& sum, const vir::Expr& label) const override {
        std::optional<std::size_t> found;
        for (std::size_t i = 0; i < sum.cases.size(); ++i) {
            if (sum.cases[i].bindings.size() == 1 && sum.cases[i].bindings[0].type == label.type) {
                if (found)
                    return std::nullopt;
                found = i;
            }
        }
        return found;
    }
};
} // namespace
const Provider& structural_provider() {
    static const StructuralProvider provider;
    return provider;
}
} // namespace cppl::decomposition
