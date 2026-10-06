// Projecting refinement types and validation expressions.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/projection.hpp"
#include "formal_projection.hpp"
#include "projector.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend {

using detail::projector::at_written_position;
using detail::projector::directives_within;
using detail::projector::Edit;
using detail::projector::Generated;
using detail::projector::generated_edit;
using detail::projector::lowering_moves_columns;
using detail::projector::Projector;
using detail::projector::resume_at;
using detail::projector::spelled_indices;
using detail::projector::spelled_tokens;
using detail::projector::token_extent;

namespace {

using detail::line_directive;

} // namespace

void Projector::project_refinements() {
    // A refinement type is runtime-bearing: the program keeps the alias it means
    // and loses only its predicate (SPEC.md REFINE-016, TRUST.md 8.1). The analysis
    // text gets the same alias, so every ordinary use of the name is Clang's, and
    // a probe stating the predicate with `self` and the indices bound.
    for (std::size_t index = 0; index < syntax.refinement_types.size(); ++index) {
        const RefinementType& refinement = syntax.refinement_types[index];
        const std::string lowering = canonical_lowering(stream, refinement);
        projection.runtime_lowerings.push_back(RuntimeLowering{refinement.range.span, lowering});
        // The alias is shorter than the declaration it lowers, but a validator
        // beside it can be longer than a declaration written on one line, and
        // would then move the code after it on that line (SPEC.md ERASE-018).
        if (lowering_moves_columns(stream, refinement.range.span, lowering)) {
            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Error;
            diagnostic.category = diagnostics::Category::UnsupportedSemantics;
            diagnostic.location = refinement.keyword_location;
            diagnostic.message = "refinement type '" + refinement.name +
                                 "' lowers to more C++ than its declaration takes on its line, so the code after it "
                                 "there would move";
            diagnostic.notes.push_back(
                diagnostics::Note{"end the line after the declaration's ';', or write the declaration across lines",
                                  refinement.keyword_location});
            projection.diagnostics.push_back(std::move(diagnostic));
        }

        const std::string suffix = std::to_string(index) + (options.unit_key.empty() ? "" : "_" + options.unit_key);
        RefinementProbe probe;
        probe.name = refinement.name;
        probe.probe = options.generated_prefix + "refinement_" + suffix;
        probe.refinement_index = index;
        probe.location = refinement.predicate_location;

        std::string parameters = refinement.indexed ? spelled_indices(stream, refinement) : std::string{};
        probe.index_count = parameters.empty() ? 0 : 1 + static_cast<std::size_t>(std::ranges::count(parameters, ','));
        if (!parameters.empty()) {
            parameters += ", ";
        }
        // `self` is an ordinary parameter of the base type, which is what makes
        // it a name Clang resolves rather than one C++L invents (SPEC.md 17.1).
        parameters += spelled_tokens(stream, refinement.base) + " self";

        Generated replacement;
        replacement += "\n";
        replacement += line_directive(refinement.keyword_location.line, refinement.keyword_location.file);
        probe.alias_offset = replacement.size() + lowering.find("using ") + 6;
        // The alias restates the base type's tokens. Where that restatement is
        // the text as written, it is a copy of it, like any other.
        if (const source::ByteSpan written = token_extent(stream, refinement.base);
            written.length != 0 && spelled_tokens(stream, refinement.base) == stream.spelling(written)) {
            const std::size_t base = lowering.find(" = ", lowering.find("using ")) + 3;
            replacement.copies.push_back(Projection::Copy{replacement.size() + base, written});
        }
        replacement += lowering.substr(0, lowering.find(';') + 1); // the alias alone
        replacement += "\n";
        replacement += line_directive(refinement.predicate_location.line, refinement.keyword_location.file);
        replacement += "[[maybe_unused]] static bool " + probe.probe + "(" + parameters + ")";

        const auto formula = detail::project_formula(stream, refinement.predicate);
        if (formula.failure) {
            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Error;
            diagnostic.category = diagnostics::Category::UnsupportedSemantics;
            diagnostic.location = refinement.predicate_location;
            diagnostic.message = "refinement type '" + refinement.name + "': " + *formula.failure;
            projection.diagnostics.push_back(std::move(diagnostic));
        }
        probe.shape = formula.shape;
        // A plain predicate is the author's own text, so it is copied where it
        // was written; a formal one is rewritten and has no such position.
        replacement += " { return (";
        if (formula.shape.kind == source::ProjectionKind::Expression) {
            replacement += at_written_position(stream, refinement.predicate);
        } else {
            replacement += formula.expression;
        }
        replacement += "); }\n";
        replacement += directives_within(stream, refinement.range.span);
        replacement += resume_at(stream, refinement.range.span.end());

        // A validation runs the predicate as written, so it must be an ordinary
        // C++ expression (SPEC.md RUNTIMECHECK-020).
        if (!refinement.validator.empty() && !formula.failure &&
            formula.shape.kind != source::ProjectionKind::Expression) {
            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Error;
            diagnostic.category = diagnostics::Category::UnsupportedSemantics;
            diagnostic.location = refinement.predicate_location;
            diagnostic.message = "refinement type '" + refinement.name +
                                 "' states a formal predicate, which no validation can evaluate at run time";
            projection.diagnostics.push_back(std::move(diagnostic));
        }

        projection.refinement_probes.push_back(std::move(probe));
        edits.push_back(generated_edit(refinement.range.span, std::move(replacement), std::nullopt, index));
    }
}

void Projector::project_validations() {
    // A validation expression calls the refinement's probe in the analysis
    // text, so Clang resolves its argument against the base type and the bridge
    // reads it as a test of that refinement; the runtime text calls the
    // validator the declaration lowers to (SPEC.md RUNTIMECHECK-018,
    // RUNTIMECHECK-021).
    //
    // A validation is runtime code, so one in proof-only syntax of a verified
    // body -- a ghost declaration, a claim that a path cannot occur, a case
    // split -- would never run, and its lowering would stand inside a span
    // erasure blanks. It is refused by name (SPEC.md RUNTIMECHECK-019).
    const auto within = [](const source::ByteSpan& inner, const source::ByteSpan& outer) {
        return inner.offset >= outer.offset && inner.end() <= outer.end();
    };
    for (const ValidationExpression& validation : syntax.validations) {
        const std::string suffix =
            std::to_string(validation.refinement_index) + (options.unit_key.empty() ? "" : "_" + options.unit_key);
        const std::string probe = options.generated_prefix + "refinement_" + suffix;
        edits.push_back(Edit{validation.callee, probe + resume_at(stream, validation.callee.end())});
        const bool in_ghost = std::ranges::any_of(syntax.ghost_declarations, [&](const GhostDeclaration& ghost) {
            return within(validation.callee, ghost.erased);
        });
        const bool in_proof_syntax = std::ranges::any_of(syntax.path_contradictions,
                                                         [&](const PathContradiction& claim) {
                                                             return within(validation.callee, claim.span);
                                                         }) ||
                                     std::ranges::any_of(syntax.path_splits, [&](const PathCaseSplit& split) {
                                         return within(validation.callee, split.span);
                                     });
        if (in_ghost || in_proof_syntax) {
            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Error;
            diagnostic.category = diagnostics::Category::UnsupportedSemantics;
            diagnostic.location = validation.location;
            diagnostic.message = std::string("a validation expression is runtime code, and ") +
                                 (in_ghost ? "a ghost declaration never runs"
                                           : "a claim that a path cannot occur or a case split never runs");
            diagnostic.notes.push_back(diagnostics::Note{
                "validate the value in the verified body and name the result there (SPEC.md RUNTIMECHECK-019)",
                validation.location});
            projection.diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        projection.runtime_lowerings.push_back(
            RuntimeLowering{validation.callee, lowered_validation(stream, syntax, validation)});
    }
}

} // namespace cppl::frontend
