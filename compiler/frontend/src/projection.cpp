#include "cppl/frontend/projection.hpp"

#include "formal_projection.hpp"

#include <algorithm>
#include <ranges>

namespace cppl::frontend {

namespace {

using detail::line_directive;

void blank(std::string& buffer, const source::ByteSpan& span) {
    const std::size_t end = std::min(span.end(), buffer.size());
    for (std::size_t offset = span.offset; offset < end; ++offset) {
        if (buffer[offset] != '\n') {
            buffer[offset] = ' ';
        }
    }
}

struct Edit {
    source::ByteSpan span;
    std::string replacement;
    std::optional<std::size_t> specification_index = std::nullopt;
};

// The tokens of a span, on one line. Two tokens the author wrote adjacently stay
// adjacent, so a type-id reads as it was written; anything between them - space,
// newline or comment - becomes one space. Restating the tokens rather than
// copying the bytes is what keeps a comment or a line break out of the result.
std::string spelled_tokens(const TokenStream& stream, const source::ByteSpan& span) {
    std::string text;
    std::size_t previous_end = 0;
    for (const Token& token : stream.tokens()) {
        if (token.span.offset < span.offset || token.span.end() > span.end()) {
            continue;
        }
        if (token.kind == TokenKind::EndOfFile) {
            break;
        }
        if (!text.empty() && token.span.offset != previous_end) {
            text += ' ';
        }
        text += token.text;
        previous_end = token.span.end();
    }
    return text;
}

// An index parameter list, with a bare name given the base type. GRAMMAR.md 16
// declares indices with ordinary parameter syntax; `type Index(n) = T where(...)`
// names one whose type is the type it refines.
std::string spelled_indices(const TokenStream& stream, const RefinementType& refinement) {
    const std::string base = spelled_tokens(stream, refinement.base);
    std::string result;
    std::string parameter;
    std::size_t token_count = 0;
    std::size_t previous_end = 0;
    const auto flush = [&] {
        if (parameter.empty()) {
            return;
        }
        if (!result.empty()) {
            result += ", ";
        }
        // A single token is a bare name, so its type is the one being refined.
        result += token_count == 1 ? base + " " + parameter : parameter;
        parameter.clear();
        token_count = 0;
    };
    for (const Token& token : stream.tokens()) {
        if (token.span.offset < refinement.indices.offset || token.span.end() > refinement.indices.end()) {
            continue;
        }
        if (token.kind == TokenKind::EndOfFile) {
            break;
        }
        if (token.is_punctuator(",")) {
            flush();
            previous_end = token.span.end();
            continue;
        }
        if (!parameter.empty() && token.span.offset != previous_end) {
            parameter += ' ';
        }
        parameter += token.text;
        previous_end = token.span.end();
        ++token_count;
    }
    flush();
    return result;
}

} // namespace

std::string canonical_lowering(const TokenStream& stream, const RefinementType& refinement) {
    std::string text;
    if (refinement.indexed) {
        text += "template <" + spelled_indices(stream, refinement) + "> ";
    }
    text += "using " + refinement.name + " = " + spelled_tokens(stream, refinement.base) + ";";
    // Every line of the declaration stays a line of the program, so nothing
    // below it moves.
    for (const char character : stream.spelling(refinement.range.span)) {
        if (character == '\n') {
            text += '\n';
        }
    }
    return text;
}

Projection project(const TokenStream& stream, const Syntax& syntax, const ProjectionOptions& options) {
    const std::string_view text = stream.text();

    Projection projection;
    projection.runtime.assign(text);

    std::vector<Edit> edits;
    edits.reserve(syntax.laws.size() + syntax.proofs.size() + syntax.pure_markers.size());

    for (const PureMarker& marker : syntax.pure_markers) {
        blank(projection.runtime, marker.keyword);
        edits.push_back(Edit{marker.keyword, std::string(marker.keyword.length, ' ')});
    }

    // A declaration becomes an ordinary C++ function stating the proposition it
    // carries, emitted where the declaration stood. Everything after this point
    // in the analysis text is C++ that Clang resolves on its own.
    const auto emit = [&stream, &projection,
                       &options](std::string_view name, std::string_view parameters, const source::ByteSpan& expression,
                                 const source::SourceLocation& begin, std::uint32_t end_line,
                                 std::size_t* name_offset = nullptr, std::string* proposition_name = nullptr) {
        std::string replacement = "\n";
        replacement += line_directive(begin.line, begin.file);
        replacement += "[[maybe_unused]] static bool ";
        if (name_offset != nullptr)
            *name_offset = replacement.size();
        replacement += name;
        replacement += "(";
        replacement += parameters;
        const auto formula = detail::project_formula(stream, expression);
        if (formula.failure) {
            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Error;
            diagnostic.category = diagnostics::Category::UnsupportedSemantics;
            diagnostic.location = begin;
            diagnostic.message = *formula.failure;
            projection.diagnostics.push_back(std::move(diagnostic));
        }
        if (formula.shape.kind != source::ProjectionKind::Expression) {
            replacement += ");\n";
            const std::string probe = options.generated_prefix + "proposition_" +
                                      std::to_string(projection.proposition_probes.size()) +
                                      (options.unit_key.empty() ? "" : "_" + options.unit_key);
            projection.proposition_probes.push_back({std::string(name), probe, begin, formula.shape});
            if (proposition_name != nullptr)
                *proposition_name = probe;
            replacement += line_directive(begin.line, begin.file);
            replacement += "[[maybe_unused]] static auto " + probe + "(";
            replacement += parameters;
            replacement += ") { return (" + formula.expression + "); }\n";
            replacement += line_directive(end_line, begin.file);
            return replacement;
        }
        replacement += ") { return (";
        replacement += stream.spelling(expression);
        replacement += "); }\n";
        replacement += line_directive(end_line, begin.file);
        return replacement;
    };

    // A refinement type is runtime-bearing: the program keeps the alias it means
    // and loses only its predicate (SPEC.md 17.4, TRUST.md 7.1). The analysis
    // text gets the same alias, so every ordinary use of the name is Clang's, and
    // a probe stating the predicate with `self` and the indices bound.
    for (std::size_t index = 0; index < syntax.refinement_types.size(); ++index) {
        const RefinementType& refinement = syntax.refinement_types[index];
        const std::string lowering = canonical_lowering(stream, refinement);
        projection.runtime_lowerings.push_back(RuntimeLowering{refinement.range.span, lowering});

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

        std::string replacement = "\n";
        replacement += line_directive(refinement.keyword_location.line, refinement.keyword_location.file);
        replacement += lowering.substr(0, lowering.find_last_of(';') + 1);
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
        replacement += " { return (" + formula.expression + "); }\n";
        replacement += line_directive(refinement.end_line, refinement.keyword_location.file);

        projection.refinement_probes.push_back(std::move(probe));
        edits.push_back(Edit{refinement.range.span, std::move(replacement)});
    }

    for (std::size_t index = 0; index < syntax.laws.size(); ++index) {
        const LawDeclaration& law = syntax.laws[index];
        blank(projection.runtime, law.range.span);

        const Clause* proposition = law.proposition();
        if (proposition == nullptr) {
            continue;
        }

        SpecificationFunction projected{law.name, index, {}};
        std::string replacement =
            emit(law.name, stream.spelling(law.parameters), proposition->expression, law.keyword_location, law.end_line,
                 &projected.analysis_offset, &projected.proposition_probe);

        // A precondition is a specification expression of the Law's own
        // parameters, so it is projected exactly like the conclusion, under a
        // generated name: the Law's name states what the Law concludes.
        if (const Clause* premise = law.premise(); premise != nullptr) {
            projected.premise_name = options.generated_prefix + "premise_" + std::to_string(index) +
                                     (options.unit_key.empty() ? "" : "_" + options.unit_key);
            replacement += emit(projected.premise_name, stream.spelling(law.parameters), premise->expression,
                                premise->location, law.end_line);
        }

        edits.push_back(Edit{law.range.span, std::move(replacement), projection.specification_functions.size()});
        projection.specification_functions.push_back(std::move(projected));
    }

    // An instantiation argument is an ordinary C++ expression written in the
    // proof's own scope, so it is projected as a function returning it. The
    // deduced return type is the type Clang gives the expression, with no
    // conversion imposed on the way out.
    const auto emit_expression = [&stream](std::string_view name, std::string_view parameters,
                                           const source::ByteSpan& expression, const source::SourceLocation& location) {
        std::string head = "[[maybe_unused]] static decltype(auto) ";
        head += name;
        head += "(";
        head += parameters;
        head += ") { return (";
        // The expression's bytes are copied verbatim, so aligning the start of
        // the copy with the column it came from makes every column inside it
        // land where the author wrote it.
        if (location.column > head.size() + 1) {
            head.append(location.column - 1 - head.size(), ' ');
        }
        head += stream.spelling(expression);
        head += "); }\n";
        return head;
    };

    for (std::size_t index = 0; index < syntax.proofs.size(); ++index) {
        const ProofDeclaration& proof = syntax.proofs[index];
        blank(projection.runtime, proof.range.span);

        const std::string suffix = std::to_string(index) + (options.unit_key.empty() ? "" : "_" + options.unit_key);

        ProofFunction projected;
        projected.name = options.generated_prefix + "proof_" + suffix;
        projected.proof_index = index;

        const std::string binding_helper = options.generated_prefix + "binding_type_" + suffix;
        std::string replacement = "template<class T> struct " + binding_helper + " { using type = T; };\n";
        replacement += emit(projected.name, stream.spelling(proof.parameters), proof.proposition,
                            proof.keyword_location, proof.end_line);

        const auto emit_steps = [&](auto&& self, const std::vector<ProofStatement>& statements,
                                    const std::string& parameters) -> void {
            for (const ProofStatement& statement : statements) {
                const auto expression_probe = [&](const source::ByteSpan& span, const source::SourceLocation& at,
                                                  std::vector<std::string>& names, std::string_view kind) {
                    std::string name =
                        options.generated_prefix + std::string(kind) + suffix + "_" + std::to_string(names.size());
                    replacement += line_directive(at.line, proof.keyword_location.file);
                    replacement += emit_expression(name, parameters, span, at);
                    replacement += line_directive(proof.end_line, proof.keyword_location.file);
                    names.push_back(std::move(name));
                };
                if (statement.kind == ProofStatementKind::Cases || statement.kind == ProofStatementKind::Decompose) {
                    expression_probe(statement.proposition, statement.location, projected.case_names, "case_");
                    const std::string subject_probe = projected.case_names.back();
                    for (const ProofArm& arm : statement.arms) {
                        // A label that is a C++ expression is resolved by Clang,
                        // like every other expression a proof mentions. A
                        // reserved label names a state that has no expression,
                        // so there is nothing to resolve.
                        if (!arm.keyword_label)
                            expression_probe(arm.label, arm.location, projected.case_names, "case_");
                        std::string scoped = parameters;
                        for (std::size_t binding = 0; binding < arm.binders.size(); ++binding) {
                            const std::string key = options.generated_prefix + "binding_" + suffix + "_" +
                                                    std::to_string(projection.binding_probes.size());
                            projection.binding_probes.push_back({key, subject_probe, arm.spelling, binding,
                                                                 statement.kind == ProofStatementKind::Decompose,
                                                                 arm.location});
                            if (!scoped.empty())
                                scoped += ", ";
                            const auto known = options.binding_types.find(key);
                            const std::string type = known == options.binding_types.end() ? "int" : known->second;
                            // Reference parameters ask Clang to resolve expressions without
                            // requiring a copy, move, default constructor or runtime object.
                            scoped += "typename ";
                            scoped += binding_helper;
                            scoped += "<";
                            scoped += type;
                            scoped += ">::type &";
                            scoped += arm.binders[binding];
                        }
                        self(self, arm.statements, scoped);
                    }
                    continue;
                }
                for (const ProofArgument& argument : statement.arguments)
                    expression_probe(argument.span, argument.location, projected.argument_names, "argument_");
                if (statement.kind != ProofStatementKind::Assume)
                    continue;
                std::string name = options.generated_prefix + "assumption_" + suffix + "_" +
                                   std::to_string(projected.assumption_names.size());
                if (detail::contains_formal_syntax(stream, statement.proposition)) {
                    replacement +=
                        emit(name, parameters, statement.proposition, statement.proposition_location, proof.end_line);
                } else {
                    replacement += line_directive(statement.proposition_location.line, proof.keyword_location.file);
                    replacement +=
                        emit_expression(name, parameters, statement.proposition, statement.proposition_location);
                    replacement += line_directive(proof.end_line, proof.keyword_location.file);
                }
                projected.assumption_names.push_back(std::move(name));
            }
        };
        emit_steps(emit_steps, proof.statements, std::string(stream.spelling(proof.parameters)));

        edits.push_back(Edit{proof.range.span, std::move(replacement)});
        projection.proof_functions.push_back(std::move(projected));
    }

    // A contract is not C++, so it leaves both texts. What Clang is given
    // instead is an ordinary function per clause, emitted after the body so
    // that everything the contract can name is already declared. The
    // postcondition takes one parameter more than the function does: `result`,
    // of the declared return type.
    for (std::size_t index = 0; index < syntax.verified_functions.size(); ++index) {
        const VerifiedFunction& verified = syntax.verified_functions[index];
        blank(projection.runtime, verified.keyword);
        blank(projection.runtime, verified.clause_region);
        edits.push_back(Edit{verified.keyword, std::string(verified.keyword.length, ' ')});
        edits.push_back(Edit{verified.clause_region,
                             projection.runtime.substr(verified.clause_region.offset, verified.clause_region.length)});

        const Clause* postcondition = verified.postcondition();
        if (postcondition == nullptr) {
            continue;
        }

        const std::string suffix = std::to_string(index) + (options.unit_key.empty() ? "" : "_" + options.unit_key);
        std::string_view parameters = stream.spelling(verified.parameters);
        const std::size_t first = parameters.find_first_not_of(" \t\r\n");
        const std::size_t last = parameters.find_last_not_of(" \t\r\n");
        if (first != std::string_view::npos && parameters.substr(first, last - first + 1) == "void") {
            parameters = {};
        }
        const bool has_parameters = parameters.find_first_not_of(" \t\r\n") != std::string_view::npos;

        std::string result_parameter;
        if (has_parameters) {
            result_parameter += parameters;
            result_parameter += ", ";
        }
        result_parameter += stream.spelling(verified.return_type);
        result_parameter += " result";

        ContractFunctions projected;
        projected.function_index = index;
        projected.postcondition_name = options.generated_prefix + "ensures_" + suffix;

        std::string replacement = emit(projected.postcondition_name, result_parameter, postcondition->expression,
                                       postcondition->location, verified.body_end_line);
        for (const Clause* precondition : verified.preconditions()) {
            std::string name = options.generated_prefix + "expects_" + suffix;
            if (!projected.precondition_names.empty()) {
                name += "_" + std::to_string(projected.precondition_names.size());
            }
            replacement +=
                emit(name, parameters, precondition->expression, precondition->location, verified.body_end_line);
            projected.precondition_names.push_back(std::move(name));
        }

        replacement += line_directive(verified.body_end_line, verified.keyword_location.file);
        replacement.append(verified.body_end_column - 1, ' ');
        edits.push_back(Edit{source::ByteSpan{verified.body_end, 0}, std::move(replacement)});
        projection.contract_functions.push_back(std::move(projected));
    }

    // A loop's clauses are not C++ either. Each invariant becomes a `bool`
    // declaration at the start of the body, in the scope the loop head sees,
    // and the text after the brace resumes at its own line and column.
    for (std::size_t index = 0; index < syntax.loops.size(); ++index) {
        const LoopSpecification& loop = syntax.loops[index];
        blank(projection.runtime, loop.clause_region);
        edits.push_back(
            Edit{loop.clause_region, projection.runtime.substr(loop.clause_region.offset, loop.clause_region.length)});

        std::string replacement = "\n";
        for (std::size_t position = 0; position < loop.invariants.size(); ++position) {
            if (detail::contains_formal_syntax(stream, loop.invariants[position].expression)) {
                diagnostics::Diagnostic diagnostic;
                diagnostic.severity = diagnostics::Severity::Error;
                diagnostic.category = diagnostics::Category::UnsupportedSemantics;
                diagnostic.location = loop.invariants[position].location;
                diagnostic.message = "formal syntax in a loop invariant is not supported yet";
                projection.diagnostics.push_back(std::move(diagnostic));
            }
            LoopInvariantMarker marker;
            marker.name = options.generated_prefix + "invariant_" + std::to_string(projection.loop_invariants.size()) +
                          (options.unit_key.empty() ? "" : "_" + options.unit_key);
            marker.loop_index = index;
            marker.function_index = loop.function_index;
            marker.location = loop.invariants[position].location;

            const source::SourceLocation& at = loop.expression_locations[position];
            replacement += line_directive(at.line, loop.keyword_location.file);
            std::string head = "[[maybe_unused]] bool " + marker.name + " = (";
            if (at.column > head.size() + 1) {
                head.append(at.column - 1 - head.size(), ' ');
            }
            replacement += head;
            replacement += stream.spelling(loop.invariants[position].expression);
            replacement += ");\n";
            projection.loop_invariants.push_back(std::move(marker));
        }
        replacement += line_directive(loop.body_open_line, loop.keyword_location.file);
        replacement.append(loop.body_open_column - 1, ' ');
        edits.push_back(Edit{source::ByteSpan{loop.body_open, 0}, std::move(replacement)});
    }

    // The runtime text is otherwise the scanned text with proof-only spans
    // blanked, so the lowerings are applied last and from the back, where no
    // offset recorded above them has moved yet.
    std::ranges::sort(projection.runtime_lowerings, [](const RuntimeLowering& lhs, const RuntimeLowering& rhs) {
        return lhs.span.offset < rhs.span.offset;
    });
    for (const RuntimeLowering& lowering : std::views::reverse(projection.runtime_lowerings)) {
        if (lowering.span.end() > projection.runtime.size()) {
            continue;
        }
        projection.runtime.replace(lowering.span.offset, lowering.span.length, lowering.text);
    }

    std::ranges::sort(edits, [](const Edit& lhs, const Edit& rhs) {
        if (lhs.span.offset != rhs.span.offset)
            return lhs.span.offset < rhs.span.offset;
        return lhs.span.length < rhs.span.length; // insert before replacing adjacent text
    });

    std::vector<std::size_t> declarations;
    declarations.reserve(syntax.pure_markers.size());
    for (const auto& marker : syntax.pure_markers)
        declarations.push_back(marker.function_offset);
    for (const auto& function : syntax.verified_functions)
        declarations.push_back(function.function_offset);
    std::ranges::sort(declarations);
    declarations.erase(std::unique(declarations.begin(), declarations.end()), declarations.end());
    std::size_t next_declaration = 0;
    std::size_t cursor = 0;
    const auto append_original = [&](std::size_t end) {
        while (next_declaration < declarations.size() && declarations[next_declaration] < end) {
            const auto original = declarations[next_declaration++];
            if (original >= cursor) {
                projection.declaration_offsets.push_back(
                    Projection::DeclarationOffset{original, projection.analysis.size() + original - cursor});
            }
        }
        projection.analysis.append(text.substr(cursor, end - cursor));
    };
    for (const Edit& edit : edits) {
        if (edit.span.offset < cursor || edit.span.end() > text.size()) {
            continue; // overlapping or out-of-range spans are never emitted
        }
        append_original(edit.span.offset);
        if (edit.specification_index.has_value()) {
            // emit() recorded the name relative to its replacement; only now
            // is its physical position in the complete analysis text known.
            projection.specification_functions[*edit.specification_index].analysis_offset += projection.analysis.size();
        }
        projection.analysis.append(edit.replacement);
        cursor = edit.span.end();
    }
    append_original(text.size());

    return projection;
}

std::optional<std::size_t> Projection::declaration_offset(std::size_t original) const {
    for (const auto& declaration : declaration_offsets) {
        if (declaration.original == original)
            return declaration.analysis;
    }
    return std::nullopt;
}

} // namespace cppl::frontend
