#include "cppl/analysis/analyze.hpp"

#include "cppl/decomposition/decomposition.hpp"
#include "cppl/elaboration/elaborate.hpp"

#include <algorithm>

namespace cppl::analysis {
namespace {
void select(clangbridge::ParseRequest& request, const frontend::Projection& projection,
            const frontend::Syntax& syntax) {
    request.selection.offsets.clear();
    request.selection.verified_offsets.clear();
    request.selection.proposition_probes.clear();
    request.selection.refinements.clear();
    for (const auto& probe : projection.proposition_probes)
        request.selection.proposition_probes.push_back({probe.name, probe.shape});
    for (const auto& refinement : projection.refinement_probes) {
        if (refinement.shape.kind != source::ProjectionKind::Expression)
            request.selection.proposition_probes.push_back({refinement.probe, refinement.shape});
        request.selection.refinements.push_back(
            {refinement.name, refinement.probe, refinement.index_count, refinement.alias_offset});
    }
    for (const auto& declaration : projection.declaration_offsets)
        request.selection.offsets.push_back(declaration.analysis);
    for (const auto& function : syntax.verified_functions) {
        if (const auto offset = projection.declaration_offset(function.function_offset))
            request.selection.verified_offsets.push_back(*offset);
    }
    for (const auto& law : projection.specification_functions)
        request.selection.offsets.push_back(law.analysis_offset);
}

std::string spelling(const decomposition::ProofBinding& binding, const vir::Type& subject) {
    if (binding.kind == decomposition::BindingKind::Alias)
        return "__underlying_type(" + subject.representation.name + ")";
    if (!binding.type.representation.name.empty())
        return binding.type.representation.name;
    if (binding.type.is_boolean())
        return "bool";
    return {};
}
} // namespace
std::expected<Result, std::string> analyze(const frontend::TokenStream& stream, const frontend::Syntax& syntax,
                                           frontend::ProjectionOptions options, clangbridge::ParseRequest request) {
    request.selection.specification_prefix = options.generated_prefix;
    for (unsigned pass = 0; pass <= 33; ++pass) {
        Result result;
        result.projection = frontend::project(stream, syntax, options);
        request.content = result.projection.analysis;
        request.recover_bindings = !result.projection.binding_probes.empty();
        request.recover_contract_types = !syntax.verified_functions.empty();
        select(request, result.projection, syntax);
        auto parsed = clangbridge::parse(request);
        if (!parsed)
            return std::unexpected(parsed.error());
        result.unit = std::move(*parsed);
        bool changed = false;
        if (result.unit.has_errors) {
            for (std::size_t index = 0; index < syntax.verified_functions.size(); ++index) {
                const auto offset =
                    result.projection.declaration_offset(syntax.verified_functions[index].function_offset);
                const auto* function = offset ? result.unit.find_at_offset(*offset) : nullptr;
                if (function && function->result.kind == clangbridge::TypeKind::Void)
                    changed = options.void_functions.insert(index).second || changed;
            }
            if (changed)
                continue;
        }
        for (const auto& probe : result.projection.binding_probes) {
            const auto* subject = result.unit.find_by_name(probe.subject);
            if (!subject)
                continue;
            auto type = elaboration::resolved_type(subject->result);
            if (!type)
                continue;
            vir::Expr value;
            value.type = *type;
            value.node = vir::ParameterRef{0, "subject"};
            auto description = decomposition::decompose({value, probe.location});
            const std::vector<decomposition::ProofBinding>* bindings = nullptr;
            if (const auto* sum = std::get_if<decomposition::SumDecomposition>(&description)) {
                if (probe.label == sum->residual.text)
                    bindings = &sum->residual_bindings;
                for (const auto& alternative : sum->cases)
                    if (alternative.label.text == probe.label)
                        bindings = &alternative.bindings;
            } else if (const auto* product = std::get_if<decomposition::ProductDecomposition>(&description)) {
                if (probe.product && probe.label == "components")
                    bindings = &product->fields;
            }
            if (!bindings || probe.index >= bindings->size())
                continue;
            const auto resolved = spelling((*bindings)[probe.index], *type);
            if (!resolved.empty() && options.binding_types[probe.key] != resolved) {
                options.binding_types[probe.key] = resolved;
                changed = true;
            }
        }
        if (!changed) {
            // The recovery AST is never a verification input if Clang rejected
            // it. The normal diagnostic pipeline fails the unit closed.
            return result;
        }
    }
    return std::unexpected("proof binding type resolution exceeded the nesting limit");
}
} // namespace cppl::analysis
