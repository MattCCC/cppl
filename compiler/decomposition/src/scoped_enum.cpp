#include "cppl/decomposition/providers.hpp"

#include <algorithm>
#include <utility>

// The scoped-enumeration provider.
//
// This is a correspondence mechanism, not an axiom. It states what Clang
// already resolved about an enumeration in the form the generic case engine
// consumes, and it introduces no logical assumption of its own: every
// proposition it produces is an ordinary comparison the kernel rechecks.
//
// What C++ states exist
//   A scoped enumeration has a fixed underlying type, and its value set is that
//   type's entire value set ([dcl.enum]). The enumerator list names some of
//   those values; it does not bound them.
//
// How those states map into proof propositions
//   Each distinct enumerator value `c` contributes the discriminator
//   `subject == c`. The residual case holds exactly when no discriminator does,
//   which the engine states as the conjunction of the negations.
//
// Why the partition is exhaustive
//   Machine comparison is total, so for each `c` a value either equals it or
//   does not. The engine splits on each discriminator in turn and the residual
//   branch is the remaining one; exhaustiveness is therefore a property of the
//   evidence the kernel checks, not a claim made here.
//
// What residual states exist
//   `unnamed`: the values of the underlying type equal to no enumerator.
//
// What bindings mean
//   `unnamed(value)` binds the subject's own value viewed at the underlying
//   type. It is an alias, so it creates no object, copy or conversion.
//
// What is deliberately not inferred
//   Nothing about provenance, storage, or which enumerators a program actually
//   produces. Enumerators sharing a value are one case, never several.
namespace cppl::decomposition {
namespace {

// The label an author writes for an enumerator, as C++ requires it to be
// written: qualified by the enumeration.
std::string qualified_label(const vir::Representation& representation, const std::string& enumerator) {
    return representation.name.empty() ? enumerator : representation.name + "::" + enumerator;
}

class ScopedEnumProvider final : public Provider {
  public:
    [[nodiscard]] std::string_view name() const override {
        return "scoped enumeration";
    }

    // A scoped enum reaches the VIR as its underlying integer carrying the
    // enumeration's resolved identity. Nothing here reads a spelling.
    [[nodiscard]] bool recognizes(const vir::Type& type) const override {
        return type.representation.is_known() && type.is_integer();
    }

    [[nodiscard]] Decomposition decompose(const Subject& subject) const override {
        const vir::Type& type = subject.expression.type;
        const vir::Representation& representation = type.representation;

        SumDecomposition sum;
        sum.exhaustiveness = ExhaustivenessModel::ResidualRequired;
        sum.residual = CaseLabel{"unnamed", false};

        // The residual binder names the subject at its underlying type. It is
        // an alias: the same value, reasoned about as a plain machine integer.
        sum.residual_bindings.push_back(
            ProofBinding{"value", type.underlying(), BindingKind::Alias, subject.expression});

        // Declaration order, one case per distinct value. Enumerators that
        // share a value are aliases of one another and name the same case.
        for (const vir::Enumerator& enumerator : representation.enumerators) {
            const auto existing = std::ranges::find_if(sum.cases, [&](const CaseDescriptor& descriptor) {
                return literal_of(descriptor) == enumerator.value;
            });
            if (existing != sum.cases.end()) {
                continue;
            }

            vir::Expr constant;
            constant.type = type;
            constant.provenance = subject.expression.provenance;
            constant.node = vir::IntLiteral{enumerator.value};

            vir::Expr discriminator;
            discriminator.type = vir::Type::boolean();
            discriminator.provenance = subject.expression.provenance;
            discriminator.node = vir::Binary{vir::BinaryOp::Equal, {subject.expression, std::move(constant)}};

            sum.cases.push_back(CaseDescriptor{
                CaseLabel{qualified_label(representation, enumerator.name), true}, std::move(discriminator), {}});
        }
        return sum;
    }

    // Clang resolves a written enumerator to its value, so any alias of a case
    // resolves to that case and a label of another enumeration does not resolve
    // at all.
    [[nodiscard]] std::optional<std::size_t> resolve_label(const SumDecomposition& decomposition,
                                                           const vir::Expr& label) const override {
        const auto* value = std::get_if<vir::IntLiteral>(&label.node);
        if (value == nullptr) {
            return std::nullopt;
        }
        for (std::size_t index = 0; index < decomposition.cases.size(); ++index) {
            if (literal_of(decomposition.cases[index]) == value->value) {
                return index;
            }
        }
        return std::nullopt;
    }

  private:
    // The enumerator value a case's discriminator compares against. The
    // discriminator is built here, so its shape is known.
    static std::int64_t literal_of(const CaseDescriptor& descriptor) {
        const auto& comparison = std::get<vir::Binary>(descriptor.discriminator.node);
        return std::get<vir::IntLiteral>(comparison.operands[1].node).value;
    }
};

} // namespace

const Provider& scoped_enum_provider() {
    static const ScopedEnumProvider provider;
    return provider;
}

} // namespace cppl::decomposition
