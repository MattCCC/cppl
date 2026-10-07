// Projecting loop clauses, claims that a path cannot occur, case splits on
// runtime paths, and the explicit instantiations of verified templates.

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
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend {

using detail::projector::at_written_position;
using detail::projector::blank;
using detail::projector::directives_within;
using detail::projector::Edit;
using detail::projector::Generated;
using detail::projector::generated_edit;
using detail::projector::Projector;
using detail::projector::resume_at;
using detail::projector::spelled_tokens;

namespace {

using detail::line_directive;

// Whether a formula's form is one a loop invariant states: a C++ condition, or
// an implication, an equivalence, a conjunction or a disjunction of such.
bool invariant_form(const source::ProjectionShape& shape) {
    using Kind = source::ProjectionKind;
    if (shape.kind == Kind::Expression) {
        return shape.children.empty();
    }
    if (shape.kind != Kind::Implication && shape.kind != Kind::Equivalence && shape.kind != Kind::Conjunction &&
        shape.kind != Kind::Disjunction) {
        return false;
    }
    return shape.children.size() == 2 && std::ranges::all_of(shape.children, invariant_form);
}

// The formula a loop invariant states where it writes formal syntax, or
// nothing for a C++ condition. A form a loop invariant does not state here -- a
// quantifier, formal equality, a memory capability -- is refused with what it
// is (GRAMMAR.md 25).
std::optional<detail::FormulaProjection> invariant_formula(const TokenStream& stream, const Clause& invariant,
                                                           std::vector<diagnostics::Diagnostic>& diagnostics) {
    if (!detail::contains_formal_syntax(stream, invariant.expression)) {
        return std::nullopt;
    }
    detail::FormulaProjection formula = detail::project_formula(stream, invariant.expression);
    if (!formula.failure && !invariant_form(formula.shape)) {
        formula.failure = "a loop invariant states a condition, or an implication, an equivalence, a conjunction or a "
                          "disjunction of conditions; a quantifier, formal equality and a memory capability are not "
                          "supported in a loop invariant yet";
    }
    if (formula.failure) {
        diagnostics::Diagnostic diagnostic;
        diagnostic.severity = diagnostics::Severity::Error;
        diagnostic.category = diagnostics::Category::UnsupportedSemantics;
        diagnostic.location = invariant.location;
        diagnostic.message = *formula.failure;
        diagnostics.push_back(std::move(diagnostic));
        return std::nullopt;
    }
    return formula;
}

} // namespace

void Projector::project_loops() {
    // A loop's clauses are not C++ either. Each invariant becomes a `bool`
    // declaration at the start of the body, in the scope the loop head sees,
    // and the text after the brace resumes at its own line and column.
    for (std::size_t index = 0; index < syntax.loops.size(); ++index) {
        const LoopSpecification& loop = syntax.loops[index];
        blank(projection.runtime, loop.clause_region, stream);
        edits.push_back(
            Edit{loop.clause_region, projection.runtime.substr(loop.clause_region.offset, loop.clause_region.length)});

        Generated replacement;
        replacement += "\n";
        for (std::size_t position = 0; position < loop.invariants.size(); ++position) {
            LoopInvariantMarker marker;
            marker.name = options.generated_prefix + "invariant_" + std::to_string(projection.loop_invariants.size()) +
                          (options.unit_key.empty() ? "" : "_" + options.unit_key);
            marker.loop_index = index;
            marker.function_index = loop.function_index;
            marker.location = loop.invariants[position].location;

            const source::SourceLocation& at = loop.expression_locations[position];
            replacement += line_directive(at.line, loop.keyword_location.file);
            // An invariant stating an implication or an equivalence is
            // projected as a contract clause's formula is, each C++ leaf in
            // the loop head's scope; the bridge reads it by its form.
            if (const std::optional<detail::FormulaProjection> formula =
                    invariant_formula(stream, loop.invariants[position], projection.diagnostics);
                formula.has_value()) {
                marker.shape = formula->shape;
                replacement += "[[maybe_unused]] auto " + marker.name + " = (";
                replacement += formula->expression;
                replacement += ");\n";
            } else {
                replacement += "[[maybe_unused]] bool " + marker.name + " = (";
                replacement += at_written_position(stream, loop.invariants[position].expression);
                replacement += ");\n";
            }
            projection.loop_invariants.push_back(std::move(marker));
        }
        // A `decreases` measure resolves in the same scope as the invariants,
        // and is an integer rather than a condition. `auto` gives it the type
        // the expression already has, which the bridge reads back. A
        // lexicographic list is one declaration per component, in order
        // (SPEC.md TERMINATION-004).
        if (loop.decreases.has_value()) {
            if (detail::contains_formal_syntax(stream, loop.decreases->expression)) {
                diagnostics::Diagnostic diagnostic;
                diagnostic.severity = diagnostics::Severity::Error;
                diagnostic.category = diagnostics::Category::UnsupportedSemantics;
                diagnostic.location = loop.decreases->location;
                diagnostic.message = "formal syntax in a loop measure is not supported yet";
                projection.diagnostics.push_back(std::move(diagnostic));
            }
            for (const MeasureComponent& component : measure_components(stream, *loop.decreases)) {
                LoopInvariantMarker marker;
                marker.name = options.generated_prefix + "measure_" +
                              std::to_string(projection.loop_invariants.size()) +
                              (options.unit_key.empty() ? "" : "_" + options.unit_key);
                marker.loop_index = index;
                marker.function_index = loop.function_index;
                marker.measure = true;
                marker.location = loop.decreases->location;

                replacement += line_directive(component.location.line, loop.keyword_location.file);
                replacement += "[[maybe_unused]] auto " + marker.name + " = (";
                replacement += at_written_position(stream, component.expression);
                replacement += ");\n";
                projection.loop_invariants.push_back(std::move(marker));
            }
        }
        replacement += resume_at(stream, loop.body_open);
        edits.push_back(generated_edit(source::ByteSpan{loop.body_open, 0}, std::move(replacement)));
    }
}

void Projector::project_path_claims() {
    for (std::size_t index = 0; index < syntax.path_contradictions.size(); ++index) {
        const PathContradiction& claim = syntax.path_contradictions[index];

        PathContradictionMarker marker;
        marker.name = claim_marker(index);
        marker.claim_index = index;
        marker.function_index = claim.function_index;
        marker.location = claim.statement.location;

        // A claim in a split's arm is erased and projected with that split.
        if (claim.split.has_value()) {
            projection.path_contradictions.push_back(std::move(marker));
            continue;
        }
        blank(projection.runtime, claim.erased, stream);
        Generated replacement = claim_block(marker.name, claim.statement);
        replacement += directives_within(stream, claim.span);
        replacement += resume_at(stream, claim.span.end());
        edits.push_back(generated_edit(claim.span, std::move(replacement)));
        projection.path_contradictions.push_back(std::move(marker));
    }
}

void Projector::project_path_splits() {
    // A case split on a runtime path is proof syntax in runtime code too. The
    // program keeps an empty statement where it stood, and Clang is given a
    // block at the same point that resolves the subject, the labels and the
    // binders in the scope the statement sees, together with every nested split
    // and claim of its arms (SPEC.md CASE-017).
    //
    // A binder is declared as a reference obtained from a function that is
    // declared and never defined: Clang needs it only to resolve what later
    // expressions in the arm say about it, and the analysis text is never
    // compiled into code. The bridge reads the binder back as the value its
    // case exposes, never as storage.
    std::set<std::size_t> helped;
    for (std::size_t index = 0; index < syntax.path_splits.size(); ++index) {
        const PathCaseSplit& split = syntax.path_splits[index];
        projection.runtime_lowerings.push_back(RuntimeLowering{split.span, erased_split(stream, split)});

        const std::string function_suffix =
            std::to_string(split.function_index) + (options.unit_key.empty() ? "" : "_" + options.unit_key);
        const std::string completes = options.generated_prefix + "split_completes_" + function_suffix;
        const std::string binder_type = options.generated_prefix + "split_type_" + function_suffix;
        const std::string binder_value = options.generated_prefix + "split_value_" + function_suffix;
        // Decomposing needs the subject's type complete, as a member access
        // would. Asking for `sizeof` in a SFINAE context instantiates a class
        // template specialization where C++ can and answers false, without an
        // error, for a type that is genuinely incomplete, which the provider
        // then refuses by name.
        if (split.function_index < syntax.verified_functions.size() && helped.insert(split.function_index).second) {
            const VerifiedFunction& verified = syntax.verified_functions[split.function_index];
            const std::size_t before =
                verified.template_header.length != 0 ? verified.template_header.offset : verified.keyword.offset;
            std::string declared = "\n";
            declared += line_directive(verified.function_location.line, verified.function_location.file);
            declared += "template<class T, class = void> struct ";
            declared += completes;
            declared += " { static constexpr bool value = false; }; template<class T> struct ";
            declared += completes;
            declared +=
                "<T, decltype(void(sizeof(T)))> { static constexpr bool value = true; }; template<class T> struct ";
            declared += binder_type;
            // Inside a class the helper is a member, and a static one, so that a
            // static member function's split resolves it without an object.
            declared += verified.member ? " { using type = T; }; template<class T> static T& "
                                        : " { using type = T; }; template<class T> T& ";
            declared += binder_value;
            declared += "();\n";
            declared += resume_at(stream, before);
            edits.push_back(Edit{source::ByteSpan{before, 0}, std::move(declared)});
        }

        const std::string& file = split.statement.location.file;
        std::size_t next_claim = 0;
        Generated replacement;
        const auto emit_split = [&](auto&& self, const ProofStatement& statement, const std::string& marker,
                                    const std::vector<std::uint32_t>& route) -> void {
            projection.path_splits.push_back(
                PathSplitMarker{marker, index, route, split.function_index, statement.location});
            replacement += "{\n";
            replacement += line_directive(statement.location.line, file);
            if (statement.location.column > 1) {
                replacement += std::string(statement.location.column - 1, ' ');
            }
            replacement += "[[maybe_unused]] bool ";
            replacement += marker;
            replacement += " = true;\n[[maybe_unused]] decltype(auto) ";
            replacement += marker;
            replacement += "_subject = (";
            replacement += at_written_position(stream, statement.proposition);
            replacement += ");\nstatic_assert(";
            replacement += completes;
            replacement += "<decltype(";
            replacement += marker;
            replacement += "_subject)>::value || true);\n";
            for (std::size_t arm = 0; arm < statement.arms.size(); ++arm) {
                if (statement.arms[arm].keyword_label) {
                    continue;
                }
                replacement += "[[maybe_unused]] decltype(auto) ";
                replacement += marker;
                replacement += "_label_";
                replacement += std::to_string(arm);
                replacement += " = (";
                replacement += at_written_position(stream, statement.arms[arm].label);
                replacement += ");\n";
            }
            std::uint32_t nested = 0;
            for (std::size_t position = 0; position < statement.arms.size(); ++position) {
                const ProofArm& arm = statement.arms[position];
                replacement += "{\n[[maybe_unused]] bool ";
                replacement += marker;
                replacement += "_arm_";
                replacement += std::to_string(position);
                replacement += " = true;\n";
                for (std::size_t binding = 0; binding < arm.binders.size(); ++binding) {
                    std::string key = marker;
                    key += "_binding_";
                    key += std::to_string(position);
                    key += "_";
                    key += std::to_string(binding);
                    const auto known = options.binding_types.find(key);
                    std::string type = "typename ";
                    type += binder_type;
                    type += "<";
                    type += known == options.binding_types.end() ? "int" : known->second;
                    type += ">::type";
                    projection.binding_probes.push_back(BindingProbe{std::move(key), marker, arm.spelling, binding,
                                                                     statement.kind == ProofStatementKind::Decompose,
                                                                     arm.location});
                    replacement += line_directive(arm.location.line, file);
                    replacement += "[[maybe_unused]] ";
                    replacement += type;
                    replacement += "& ";
                    replacement += arm.binders[binding];
                    replacement += " = ";
                    replacement += binder_value;
                    replacement += "<";
                    replacement += type;
                    replacement += ">();\n";
                }
                for (std::size_t written = 0; written < arm.statements.size(); ++written) {
                    const ProofStatement& inner = arm.statements[written];
                    if (inner.kind == ProofStatementKind::Contradiction) {
                        if (next_claim < split.claims.size()) {
                            const std::size_t claim = split.claims[next_claim++];
                            replacement +=
                                claim_block(claim_marker(claim), syntax.path_contradictions[claim].statement);
                        }
                        continue;
                    }
                    std::vector<std::uint32_t> inner_route = route;
                    inner_route.push_back(static_cast<std::uint32_t>(position));
                    inner_route.push_back(static_cast<std::uint32_t>(written));
                    self(self, inner, marker + "_nested_" + std::to_string(nested++), inner_route);
                }
                replacement += "}\n";
            }
            replacement += "}\n";
        };
        emit_split(emit_split, split.statement,
                   options.generated_prefix + "split_" + std::to_string(index) +
                       (options.unit_key.empty() ? "" : "_" + options.unit_key),
                   {});
        replacement += directives_within(stream, split.span);
        replacement += resume_at(stream, split.span.end());
        edits.push_back(generated_edit(split.span, std::move(replacement)));
    }
}

void Projector::project_explicit_instantiations() {
    // An explicit instantiation instantiates a body in this unit, but Clang's
    // cursor API exposes no cursor for it, so nothing would reach the
    // specialization that now has a contract to discharge. A reference to it
    // supplies the same edge an ordinary use would, and the specialization is
    // then collected exactly as every other one is (SPEC.md TEMPLATE-001).
    //
    // The reference is an edit, so it exists only in the analysis text. The
    // runtime text keeps the instantiation the author wrote and gains nothing,
    // which is what keeps this out of the emitted program (AGENTS.md 16).
    for (std::size_t index = 0; index < syntax.explicit_instantiations.size(); ++index) {
        const ExplicitInstantiation& instantiation = syntax.explicit_instantiations[index];
        // Only an instantiation of a function template this unit marked
        // verified needs the edge, and only such a unit has opted into C++L.
        // An ordinary C++ program's instantiations are left exactly as written,
        // so nothing about them depends on this recognition (AGENTS.md 2).
        const bool verified_here =
            std::ranges::any_of(syntax.verified_functions, [&](const VerifiedFunction& candidate) {
                return candidate.template_header.length != 0 && !candidate.explicit_specialization &&
                       candidate.function_name == instantiation.function_name;
            });
        if (!verified_here) {
            continue;
        }
        const std::string name = options.generated_prefix + "instantiate_" + std::to_string(index) +
                                 (options.unit_key.empty() ? "" : "_" + options.unit_key);
        std::string reference = "\n";
        reference += line_directive(instantiation.location.line, instantiation.location.file);
        reference +=
            "[[maybe_unused]] static auto " + name + " = &" + spelled_tokens(stream, instantiation.id_expression) + ";";
        reference += resume_at(stream, instantiation.insertion_offset);
        edits.push_back(Edit{source::ByteSpan{instantiation.insertion_offset, 0}, std::move(reference)});
    }
}

} // namespace cppl::frontend
