// Elaborating case analyses: matching each arm to the states a
// representation has, and the splits written on a runtime path.

#include "cppl/clang/ast.hpp"
#include "cppl/decomposition/decomposition.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/types.hpp"
#include "elaborate_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::elaboration {

using detail::elaborator::Coverage;
using detail::elaborator::exhaustive;
using detail::elaborator::ExpressionElaborator;
using detail::elaborator::match_arm;
using detail::elaborator::MatchedArm;
using detail::elaborator::product_arm;
using detail::elaborator::record_states;
using detail::elaborator::report;

namespace detail::elaborator {

// Matches one written arm to the case it denotes: a reserved label against the
// partition's own labels, and an expression label by what Clang resolved it to
// (`label`), as the provider reads it. A case claimed twice, a label that is
// not a case of the subject, and an arm naming a number of binders its case
// does not supply are refused here. This is the one rule for arms, whether the
// split stands in a proof body or on a runtime path (SPEC.md CASE-004,
// CASE-006, CASE-017).
std::optional<MatchedArm> match_arm(const frontend::ProofArm& arm, const vir::Expr* label,
                                    const decomposition::SumDecomposition& sum, const decomposition::Provider& provider,
                                    const vir::Type& subject, Coverage& coverage, diagnostics::Engine& engine) {
    const bool residual_required = sum.exhaustiveness == decomposition::ExhaustivenessModel::ResidualRequired;
    MatchedArm matched;
    if (arm.keyword_label) {
        // A reserved label names the residual state, so it is matched against
        // the partition's own residual label rather than resolved as an
        // expression.
        const std::string& written = arm.spelling;
        const auto named =
            std::ranges::find_if(sum.cases, [&](const auto& candidate) { return candidate.label.text == written; });
        if (named != sum.cases.end()) {
            const auto index = static_cast<std::size_t>(named - sum.cases.begin());
            if (coverage.named[index]) {
                report(engine, diagnostics::Category::Elaboration, arm.location, "duplicate case '" + written + "'");
                return std::nullopt;
            }
            coverage.named[index] = true;
            matched.descriptor = static_cast<std::uint32_t>(index);
            matched.label = written;
            matched.bindings = &named->bindings;
        } else {
            if (!residual_required || written != sum.residual.text) {
                report(engine, diagnostics::Category::Elaboration, arm.location,
                       "'" + written + "' is not a case of '" + describe(subject) + "'");
                return std::nullopt;
            }
            if (coverage.residual) {
                report(engine, diagnostics::Category::Elaboration, arm.location, "duplicate case '" + written + "'");
                return std::nullopt;
            }
            coverage.residual = true;
            matched.label = sum.residual.text;
            matched.bindings = &sum.residual_bindings;
        }
    } else {
        const std::optional<std::size_t> index =
            label != nullptr ? provider.resolve_label(sum, *label) : std::optional<std::size_t>{};
        if (!index || !(label->type == subject)) {
            report(engine, diagnostics::Category::Elaboration, arm.location,
                   "this label does not name a case of '" + describe(subject) + "'");
            return std::nullopt;
        }
        const decomposition::CaseDescriptor& descriptor = sum.cases[*index];
        if (coverage.named[*index]) {
            report(engine, diagnostics::Category::Elaboration, arm.location,
                   "duplicate case '" + descriptor.label.text + "'", "labels that denote one state name one case");
            return std::nullopt;
        }
        coverage.named[*index] = true;
        matched.descriptor = static_cast<std::uint32_t>(*index);
        matched.label = descriptor.label.text;
        matched.bindings = &descriptor.bindings;
    }

    // A case supplies exactly the bindings its provider describes, so an arm
    // names exactly that many. An omitted case has no body, so it binds
    // nothing.
    if (!arm.omitted && arm.binders.size() != matched.bindings->size()) {
        report(engine, diagnostics::Category::Elaboration, arm.location,
               "case '" + matched.label + "' binds " + std::to_string(matched.bindings->size()) +
                   " value(s), but this arm names " + std::to_string(arm.binders.size()));
        return std::nullopt;
    }
    return matched;
}

// Exhaustiveness comes from the partition the provider described. A state it
// lists and no arm claims is a missing case, so adding a state to a
// representation makes a split that did not account for it stop being
// exhaustive (SPEC.md CASE-004, CASE-005).
bool exhaustive(const decomposition::SumDecomposition& sum, const Coverage& coverage, const vir::Type& subject,
                const source::SourceLocation& location, diagnostics::Engine& engine) {
    for (std::size_t index = 0; index < sum.cases.size(); ++index) {
        if (!coverage.named[index]) {
            report(engine, diagnostics::Category::ProofFailure, location,
                   "non-exhaustive cases: '" + sum.cases[index].label.text + "' has no arm");
            return false;
        }
    }
    if (sum.exhaustiveness == decomposition::ExhaustivenessModel::ResidualRequired && !coverage.residual) {
        report(engine, diagnostics::Category::ProofFailure, location,
               "non-exhaustive cases: '" + sum.residual.text + "' has no arm",
               "'" + describe(subject) +
                   "' has states beyond the ones it names, and "
                   "no wildcard absorbs them");
        return false;
    }
    return true;
}

// A product is decomposed by one `components` arm naming every component. A
// sum is never decomposed, and a product never split into cases (SPEC.md
// CASE-003, CASE-007).
bool product_arm(const frontend::ProofStatement& statement, const decomposition::ProductDecomposition& product,
                 diagnostics::Engine& engine) {
    if (statement.kind != frontend::ProofStatementKind::Decompose || statement.arms.size() != 1 ||
        statement.arms[0].spelling != "components") {
        report(engine, diagnostics::Category::Elaboration, statement.location,
               "product decomposition requires decompose subject { components(binders) => { proof } }");
        return false;
    }
    const auto& arm = statement.arms[0];
    if (arm.binders.size() != product.fields.size()) {
        report(engine, diagnostics::Category::Elaboration, arm.location,
               "product binds " + std::to_string(product.fields.size()) + " value(s), but this arm names " +
                   std::to_string(arm.binders.size()));
        return false;
    }
    return true;
}

// Records what the engine decided about one statement's subject, for editors.
// This is a read-only byproduct: nothing consults it while elaborating, so it
// cannot change which proofs or bodies are accepted. A statement elaborated
// more than once, as a split on a runtime path is once per path reaching it, is
// recorded once.
void record_states(const frontend::ProofStatement& statement, const vir::Type& subject,
                   const decomposition::Decomposition& decomposed, std::vector<SubjectStates>* recorded) {
    const decomposition::Provider* modeled = decomposition::provider_for(subject);
    if (recorded == nullptr || modeled == nullptr || std::ranges::any_of(*recorded, [&](const SubjectStates& entry) {
            return entry.location == statement.location;
        })) {
        return;
    }
    SubjectStates record;
    record.location = statement.location;
    record.subject = statement.reference;
    record.representation = describe(subject);
    record.provider = std::string(modeled->name());
    if (const auto* product = std::get_if<decomposition::ProductDecomposition>(&decomposed)) {
        record.product = true;
        SubjectStates::State components{"components", {}, false};
        for (const auto& field : product->fields)
            components.binders.push_back(field.name);
        record.states.push_back(std::move(components));
    } else if (const auto* sum = std::get_if<decomposition::SumDecomposition>(&decomposed)) {
        for (const auto& described_case : sum->cases) {
            SubjectStates::State state{described_case.label.text, {}, false};
            for (const auto& binding : described_case.bindings)
                state.binders.push_back(binding.name);
            record.states.push_back(std::move(state));
        }
        if (sum->exhaustiveness == decomposition::ExhaustivenessModel::ResidualRequired) {
            SubjectStates::State state{sum->residual.text, {}, true};
            for (const auto& binding : sum->residual_bindings)
                state.binders.push_back(binding.name);
            record.states.push_back(std::move(state));
        }
    } else {
        return;
    }
    recorded->push_back(std::move(record));
}

} // namespace detail::elaborator

std::optional<vir::Expr> ExpressionElaborator::convert_split(const clangbridge::CaseSplit& split,
                                                             const clangbridge::Expr& expr, vir::Expr result) {
    const auto refuse = [&](std::string reason) -> std::optional<vir::Expr> {
        failure_ = Failure{std::move(reason), expr.location};
        return std::nullopt;
    };
    const auto refused = [&]() -> std::optional<vir::Expr> {
        refused_ = true;
        return refuse("a case split in its body was refused");
    };
    const frontend::ProofStatement* statement = nullptr;
    if (splits_ != nullptr) {
        if (const auto found = splits_->find(split.marker); found != splits_->end()) {
            statement = found->second;
        }
    }
    if (statement == nullptr || engine_ == nullptr) {
        return refuse("a case split is written where this implementation does not read one");
    }
    if (split.operands.size() < 1 + split.arms.size() || split.arms.size() != statement->arms.size()) {
        return refuse("malformed case split");
    }
    const std::size_t first_arm = split.operands.size() - split.arms.size();

    std::optional<vir::Expr> subject = convert(split.operands.front());
    if (!subject) {
        return std::nullopt;
    }
    const decomposition::Decomposition decomposed = decomposition::decompose({*subject, statement->location});
    if (const auto* unsupported = std::get_if<decomposition::Unsupported>(&decomposed)) {
        report(*engine_, diagnostics::Category::UnsupportedSemantics, statement->location,
               "proof decomposition is not defined for '" + unsupported->representation + "'", unsupported->reason);
        return refused();
    }
    record_states(*statement, subject->type, decomposed, states_);

    vir::CaseSplit converted;
    std::vector<std::vector<vir::Expr>> values(split.arms.size());
    std::vector<vir::Expr> discriminators;
    if (const auto* product = std::get_if<decomposition::ProductDecomposition>(&decomposed)) {
        if (!product_arm(*statement, *product, *engine_)) {
            return refused();
        }
        converted.product = true;
        converted.arms.push_back(vir::CaseSplit::Arm{std::nullopt, "components", statement->arms[0].location});
        for (const auto& field : product->fields) {
            vir::Expr value = field.value;
            value.type = field.type;
            values[0].push_back(std::move(value));
        }
    } else {
        if (statement->kind == frontend::ProofStatementKind::Decompose) {
            report(*engine_, diagnostics::Category::Elaboration, statement->location,
                   "decompose requires a product; use cases for alternative states");
            return refused();
        }
        const auto& sum = std::get<decomposition::SumDecomposition>(decomposed);
        const decomposition::Provider& provider = *decomposition::provider_for(subject->type);
        Coverage coverage{std::vector<bool>(sum.cases.size(), false), false};
        for (std::size_t position = 0; position < split.arms.size(); ++position) {
            const frontend::ProofArm& arm = statement->arms[position];
            std::optional<vir::Expr> label;
            if (split.arms[position].label.has_value()) {
                if (*split.arms[position].label >= first_arm) {
                    return refuse("malformed case split");
                }
                label = convert(split.operands[*split.arms[position].label]);
                if (!label) {
                    return std::nullopt;
                }
            } else if (!arm.keyword_label) {
                return refuse("malformed case split");
            }
            const std::optional<MatchedArm> matched =
                match_arm(arm, label ? &*label : nullptr, sum, provider, subject->type, coverage, *engine_);
            if (!matched) {
                return refused();
            }
            converted.arms.push_back(vir::CaseSplit::Arm{matched->descriptor, matched->label, arm.location});
            if (!arm.omitted) {
                for (const auto& binding : *matched->bindings) {
                    vir::Expr value = binding.value;
                    value.type = binding.type;
                    values[position].push_back(std::move(value));
                }
            }
        }
        if (!exhaustive(sum, coverage, subject->type, statement->location, *engine_)) {
            return refused();
        }
        converted.residual = sum.exhaustiveness == decomposition::ExhaustivenessModel::ResidualRequired;
        for (const auto& described : sum.cases) {
            discriminators.push_back(described.discriminator);
        }
    }

    converted.discriminators = static_cast<std::uint32_t>(discriminators.size());
    converted.operands.push_back(std::move(*subject));
    for (auto& discriminator : discriminators) {
        converted.operands.push_back(std::move(discriminator));
    }
    for (std::size_t position = 0; position < split.arms.size(); ++position) {
        if (split.arms[position].binders != values[position].size()) {
            return refuse("malformed case split");
        }
        for (std::uint32_t index = 0; index < values[position].size(); ++index) {
            binders_.insert_or_assign(std::tuple{split.marker, static_cast<std::uint32_t>(position), index},
                                      values[position][index]);
        }
        std::optional<vir::Expr> continued = convert(split.operands[first_arm + position]);
        for (std::uint32_t index = 0; index < values[position].size(); ++index) {
            binders_.erase(std::tuple{split.marker, static_cast<std::uint32_t>(position), index});
        }
        if (!continued) {
            return std::nullopt;
        }
        // An omitted case's arm is its claim and nothing else, and that
        // claim is the omission's own (SPEC.md CASE-004, CASE-012).
        const auto* claim = std::get_if<vir::PathContradiction>(&continued->node);
        if (statement->arms[position].omitted != (claim != nullptr && claim->omitted.has_value())) {
            return refuse("malformed omitted case");
        }
        converted.operands.push_back(std::move(*continued));
    }
    result.node = std::move(converted);
    return result;
}

} // namespace cppl::elaboration
