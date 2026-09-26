#include "cppl/frontend/projection.hpp"

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/projection.hpp"
#include "formal_projection.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// Generated analysis text, and every run of it copied byte for byte from the
// scanned text, at offsets relative to the start of `text`.
struct Generated {
    std::string text;
    std::vector<Projection::Copy> copies;

    Generated& operator+=(std::string_view plain) {
        text += plain;
        return *this;
    }
    Generated& operator+=(const Generated& more) {
        for (const Projection::Copy& copy : more.copies) {
            copies.push_back(Projection::Copy{text.size() + copy.analysis, copy.original});
        }
        text += more.text;
        return *this;
    }
    // Appends the scanned text of `span`, recorded as a copy of it.
    void copy(const TokenStream& stream, const source::ByteSpan& span) {
        if (span.length != 0) {
            copies.push_back(Projection::Copy{text.size(), span});
        }
        text += stream.spelling(span);
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return text.size();
    }
    [[nodiscard]] bool empty() const noexcept {
        return text.empty();
    }
};

struct Edit {
    source::ByteSpan span;
    std::string replacement;
    std::optional<std::size_t> specification_index = std::nullopt;
    std::optional<std::size_t> refinement_index = std::nullopt;
    std::vector<Projection::Copy> copies = {};
};

Edit generated_edit(const source::ByteSpan& span, Generated replacement,
                    std::optional<std::size_t> specification_index = std::nullopt,
                    std::optional<std::size_t> refinement_index = std::nullopt) {
    return Edit{span, std::move(replacement.text), specification_index, refinement_index,
                std::move(replacement.copies)};
}

// An expression the author wrote, copied on a line of its own that starts at the
// line and column its first token was written at, so a diagnostic anywhere
// inside it points where the author wrote it rather than into the generated
// declaration around it. The bytes are copied verbatim, so every later token
// keeps its column too. Without a file to name in a line directive, or with no
// token to anchor on, the text is copied in place.
Generated at_written_position(const TokenStream& stream, const source::ByteSpan& expression) {
    Generated text;
    const std::vector<Token>& tokens = stream.tokens();
    const auto first =
        std::ranges::lower_bound(tokens, expression.offset, {}, [](const Token& token) { return token.span.offset; });
    if (first == tokens.end() || first->kind == TokenKind::EndOfFile || first->span.offset >= expression.end()) {
        text.copy(stream, expression);
        return text;
    }
    const source::SourceLocation at = stream.location_of(*first);
    const std::string directive = line_directive(at.line, at.file);
    if (directive.empty()) {
        text.copy(stream, expression);
        return text;
    }
    text += "\n" + directive;
    text += std::string(at.column > 1 ? at.column - 1 : 0, ' ');
    text.copy(stream, source::ByteSpan{first->span.offset, expression.end() - first->span.offset});
    text += "\n";
    return text;
}

// A line directive and indentation after which the text resumes at the position
// the token at `offset` was written at, so an insertion before it moves nothing
// a diagnostic points at.
std::string resume_at(const TokenStream& stream, std::size_t offset) {
    const std::vector<Token>& tokens = stream.tokens();
    const auto at = std::ranges::lower_bound(tokens, offset, {}, [](const Token& token) { return token.span.offset; });
    if (at == tokens.end() || at->kind == TokenKind::EndOfFile) {
        return {};
    }
    const source::SourceLocation location = stream.location_of(*at);
    std::string text = line_directive(location.line, location.file);
    text.append(location.column > 1 ? location.column - 1 : 0, ' ');
    return text;
}

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

// The bytes from the first token of a span to the end of its last, without the
// space around them.
source::ByteSpan token_extent(const TokenStream& stream, const source::ByteSpan& span) {
    std::size_t begin = span.end();
    std::size_t end = span.offset;
    for (const Token& token : stream.tokens()) {
        if (token.kind == TokenKind::EndOfFile || token.span.offset >= span.end()) {
            break;
        }
        if (token.span.offset >= span.offset && token.span.end() <= span.end()) {
            begin = std::min(begin, token.span.offset);
            end = std::max(end, token.span.end());
        }
    }
    return end > begin ? source::ByteSpan{begin, end - begin} : source::ByteSpan{};
}

// Index declarations use ordinary typed C++ parameter syntax. Never infer a
// parameter type from the refined base or create an alternate syntax.
std::string spelled_indices(const TokenStream& stream, const RefinementType& refinement) {
    return spelled_tokens(stream, refinement.indices);
}

// The names a template header introduces, as an argument list: from
// `template <unsigned N, typename T>` this yields `N, T`.
//
// A probe declared under that header is a template too, and a template is
// instantiated only where it is used. Naming the probe at these arguments
// inside the body is what makes C++ instantiate it alongside each
// specialization of the function, at the same arguments (SPEC.md TEMPLATE-001).
//
// The name of each parameter is the last identifier before the `,` or `>` that
// ends it, which is where C++ puts it in every form this implementation
// accepts. A parameter pack or a defaulted parameter is not one of those forms,
// and yields no name, so the caller emits nothing rather than something wrong.
std::optional<std::string> template_parameter_names(const TokenStream& stream, const source::ByteSpan& header) {
    const std::string_view text = stream.spelling(header);
    const std::size_t open = text.find('<');
    if (open == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t close = text.rfind('>');
    if (close == std::string_view::npos || close <= open) {
        return std::nullopt;
    }
    const std::string_view inside = text.substr(open + 1, close - open - 1);
    std::string names;
    std::string candidate;
    int depth = 0;
    const auto flush = [&] {
        if (candidate.empty()) {
            return false;
        }
        if (!names.empty()) {
            names += ", ";
        }
        names += candidate;
        candidate.clear();
        return true;
    };
    for (std::size_t index = 0; index <= inside.size(); ++index) {
        const char character = index < inside.size() ? inside[index] : ',';
        if (character == '<' || character == '(') {
            ++depth;
            continue;
        }
        if (character == '>' || character == ')') {
            --depth;
            continue;
        }
        if (depth != 0) {
            continue;
        }
        if (character == ',') {
            if (!flush()) {
                return std::nullopt;
            }
            continue;
        }
        // `...` introduces a pack and `=` a default argument; neither is a form
        // whose instantiation can be forced by naming the parameters.
        if (character == '.' || character == '=') {
            return std::nullopt;
        }
        if ((std::isalnum(static_cast<unsigned char>(character)) != 0) || character == '_') {
            candidate += character;
            continue;
        }
        candidate.clear();
    }
    return names.empty() ? std::nullopt : std::optional<std::string>{names};
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

std::string erased_split(const TokenStream& stream, const PathCaseSplit& split) {
    std::string text(stream.spelling(split.span));
    for (char& character : text) {
        if (character != '\n') {
            character = ' ';
        }
    }
    if (!text.empty()) {
        text.back() = ';';
    }
    return text;
}

const ProofStatement* split_statement(const Syntax& syntax, const PathSplitMarker& marker) {
    if (marker.split_index >= syntax.path_splits.size() || marker.route.size() % 2 != 0) {
        return nullptr;
    }
    const ProofStatement* statement = &syntax.path_splits[marker.split_index].statement;
    for (std::size_t step = 0; step < marker.route.size(); step += 2) {
        if (marker.route[step] >= statement->arms.size()) {
            return nullptr;
        }
        const ProofArm& arm = statement->arms[marker.route[step]];
        if (marker.route[step + 1] >= arm.statements.size()) {
            return nullptr;
        }
        statement = &arm.statements[marker.route[step + 1]];
    }
    return statement;
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

    // `unsafe` is a marker: the word leaves both texts and the declaration or
    // the block it marks stays ordinary C++ (SPEC.md ERASE-003, Annex M).
    for (const UnsafeFunction& function : syntax.unsafe_functions) {
        blank(projection.runtime, function.keyword);
        edits.push_back(Edit{function.keyword, std::string(function.keyword.length, ' ')});
    }
    // A block's region starts at a declaration only Clang sees, just inside its
    // `{`, written at the position of the word it replaces, so the region's
    // provenance is where the author wrote `unsafe`.
    for (std::size_t index = 0; index < syntax.unsafe_blocks.size(); ++index) {
        const UnsafeBlock& block = syntax.unsafe_blocks[index];
        blank(projection.runtime, block.keyword);
        edits.push_back(Edit{block.keyword, std::string(block.keyword.length, ' ')});
        if (block.nested) {
            continue;
        }
        UnsafeBlockMarker marker;
        marker.name = options.generated_prefix + "unsafe_" + std::to_string(index) +
                      (options.unit_key.empty() ? "" : "_" + options.unit_key);
        marker.block_index = index;
        marker.function_index = block.function_index;
        marker.location = block.location;
        // The declared name, which is where Clang locates a declaration, stands
        // exactly where `unsafe` was written.
        std::string inserted = "\n[[maybe_unused]] bool\n";
        inserted += line_directive(block.location.line, block.location.file);
        inserted += std::string(block.location.column > 1 ? block.location.column - 1 : 0, ' ');
        inserted += marker.name + " = true;\n";
        inserted += line_directive(block.body_open_line, block.location.file);
        inserted += std::string(block.body_open_column > 1 ? block.body_open_column - 1 : 0, ' ');
        edits.push_back(Edit{source::ByteSpan{block.body_open, 0}, std::move(inserted)});
        projection.unsafe_blocks.push_back(std::move(marker));
    }

    // A ghost declaration leaves the program whole (SPEC.md GHOST-001,
    // ERASE-011). Clang still sees it, after a declaration only Clang sees,
    // named where `ghost` was written, which tells the body lowering that the
    // declaration after it is ghost state.
    for (std::size_t index = 0; index < syntax.ghost_declarations.size(); ++index) {
        const GhostDeclaration& ghost = syntax.ghost_declarations[index];
        blank(projection.runtime, ghost.erased);
        const std::string name = options.generated_prefix + "ghost_" + std::to_string(index) +
                                 (options.unit_key.empty() ? "" : "_" + options.unit_key);
        const std::size_t column = ghost.location.column > 1 ? ghost.location.column - 1 : 0;
        std::string inserted = "\n[[maybe_unused]] bool\n";
        inserted += line_directive(ghost.location.line, ghost.location.file);
        inserted += std::string(column, ' ') + name + " = true;\n";
        inserted += line_directive(ghost.location.line, ghost.location.file);
        inserted += std::string(column + ghost.keyword.length, ' ');
        edits.push_back(Edit{ghost.keyword, std::move(inserted)});
    }

    // The template header the declaration being emitted stands under, where it
    // has one. A contract clause may name the template's parameters, so its
    // probe has to be declared under the same header; laws and proofs are not
    // templated and leave this empty.
    std::string template_header;

    // Whether the declaration being projected is a template, as opposed to an
    // explicit specialization whose `template <>` declares no parameters. A
    // specialization's probes are ordinary functions: its arguments are fixed,
    // so there is nothing to specialize and nothing to instantiate.
    bool template_parameters = false;

    // Whether the declaration being projected is a member function with an
    // implicit object. Its probes are then members of the same class, stated
    // `const`: a contract is resolved in the scope the body sees, with `this`
    // and ordinary member lookup (SPEC.md CONTRACT-008), and reads the object
    // without writing it. A static member's probes are static members, which
    // the ordinary `static` prefix already declares inside a class.
    bool implicit_object = false;

    // What every generated declaration is introduced by. A templated probe
    // cannot be `static`: it is a template, and its header has to precede the
    // declaration it introduces. Nor can a probe of a member function with an
    // implicit object: it has one too.
    const auto templated = [&template_parameters] {
        return template_parameters;
    };
    const auto declaration_prefix = [&template_header, &templated, &implicit_object] {
        std::string prefix;
        if (templated()) {
            prefix += template_header;
            prefix += " [[maybe_unused]] ";
            return prefix;
        }
        if (implicit_object) {
            return std::string("[[maybe_unused]] ");
        }
        prefix += "[[maybe_unused]] static ";
        return prefix;
    };
    // What follows a probe's parameter list: `const` for a member function's
    // probe, which reads the implicit object and never writes it.
    const auto probe_qualifier = [&implicit_object] {
        return implicit_object ? std::string(" const") : std::string();
    };

    // A declaration becomes an ordinary C++ function stating the proposition it
    // carries, emitted where the declaration stood. Everything after this point
    // in the analysis text is C++ that Clang resolves on its own.
    const auto emit = [&stream, &projection, &options, &declaration_prefix, &probe_qualifier](
                          std::string_view name, const Generated& parameters, const source::ByteSpan& expression,
                          const source::SourceLocation& begin, std::uint32_t end_line,
                          std::size_t* name_offset = nullptr, std::string* proposition_name = nullptr) {
        const std::string prefix = declaration_prefix();
        const std::string qualifier = probe_qualifier();
        Generated replacement;
        replacement += "\n";
        replacement += line_directive(begin.line, begin.file);
        replacement += prefix + "bool ";
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
            replacement += ")" + qualifier + ";\n";
            const std::string probe = options.generated_prefix + "proposition_" +
                                      std::to_string(projection.proposition_probes.size()) +
                                      (options.unit_key.empty() ? "" : "_" + options.unit_key);
            projection.proposition_probes.push_back({std::string(name), probe, begin, formula.shape});
            if (proposition_name != nullptr)
                *proposition_name = probe;
            replacement += line_directive(begin.line, begin.file);
            replacement += prefix + "auto " + probe + "(";
            replacement += parameters;
            // A memory capability states storage permission, not a value, so its
            // probe body is a statement: there is nothing to return, and the
            // operands are present only so Clang resolves them (SPEC.md 12.10).
            const bool capability = formula.shape.kind == source::ProjectionKind::Readable ||
                                    formula.shape.kind == source::ProjectionKind::Writable ||
                                    formula.shape.kind == source::ProjectionKind::Capabilities;
            replacement += capability ? ")" + qualifier + " { " + formula.expression + "; }\n"
                                      : ")" + qualifier + " { return (" + formula.expression + "); }\n";
            replacement += line_directive(end_line, begin.file);
            return replacement;
        }
        replacement += ")" + qualifier + " { return (";
        replacement += at_written_position(stream, expression);
        replacement += "); }\n";
        replacement += line_directive(end_line, begin.file);
        return replacement;
    };

    // A refinement type is runtime-bearing: the program keeps the alias it means
    // and loses only its predicate (SPEC.md REFINE-016, TRUST.md 8.1). The analysis
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
        // A plain predicate is the author's own text, so it is copied where it
        // was written; a formal one is rewritten and has no such position.
        replacement += " { return (";
        if (formula.shape.kind == source::ProjectionKind::Expression) {
            replacement += at_written_position(stream, refinement.predicate);
        } else {
            replacement += formula.expression;
        }
        replacement += "); }\n";
        replacement += line_directive(refinement.end_line, refinement.keyword_location.file);

        projection.refinement_probes.push_back(std::move(probe));
        edits.push_back(generated_edit(refinement.range.span, std::move(replacement), std::nullopt, index));
    }

    for (std::size_t index = 0; index < syntax.laws.size(); ++index) {
        const LawDeclaration& law = syntax.laws[index];
        blank(projection.runtime, law.range.span);

        const Clause* proposition = law.proposition();
        if (proposition == nullptr) {
            continue;
        }

        SpecificationFunction projected{law.name, index, {}};
        Generated parameters;
        parameters.copy(stream, law.parameters);
        Generated replacement = emit(law.name, parameters, proposition->expression, law.keyword_location, law.end_line,
                                     &projected.analysis_offset, &projected.proposition_probe);

        // A precondition is a specification expression of the Law's own
        // parameters, so it is projected exactly like the conclusion, under a
        // generated name: the Law's name states what the Law concludes.
        if (const Clause* premise = law.premise(); premise != nullptr) {
            projected.premise_name = options.generated_prefix + "premise_" + std::to_string(index) +
                                     (options.unit_key.empty() ? "" : "_" + options.unit_key);
            replacement +=
                emit(projected.premise_name, parameters, premise->expression, premise->location, law.end_line);
        }

        edits.push_back(
            generated_edit(law.range.span, std::move(replacement), projection.specification_functions.size()));
        projection.specification_functions.push_back(std::move(projected));
    }

    // An instantiation argument is an ordinary C++ expression written in the
    // proof's own scope, so it is projected as a function returning it. The
    // deduced return type is the type Clang gives the expression, with no
    // conversion imposed on the way out.
    const auto emit_expression = [&stream](std::string_view name, const Generated& parameters,
                                           const source::ByteSpan& expression) {
        Generated head;
        head += "[[maybe_unused]] static decltype(auto) ";
        head += name;
        head += "(";
        head += parameters;
        head += ") { return (";
        head += at_written_position(stream, expression);
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
        Generated replacement;
        replacement += "template<class T> struct " + binding_helper + " { using type = T; };\n";
        // Decomposing a subject needs its type complete, as a member access would
        // (SPEC.md 20.4), but a subject reached through a reference never makes
        // C++ instantiate a class template specialization. Asking for `sizeof` of
        // a subject probe's result in a SFINAE context instantiates it where C++
        // can, and answers false without an error for a type that is genuinely
        // incomplete, which the provider then refuses by name.
        const std::string completion_helper = options.generated_prefix + "completes_" + suffix;
        replacement += "template<class F, class = void> struct ";
        replacement += completion_helper;
        replacement += " { static constexpr bool value = false; };\ntemplate<class R, class... A> struct ";
        replacement += completion_helper;
        replacement += "<R (*)(A...), decltype(void(sizeof(R)))> { static constexpr bool value = true; };\n";
        Generated proof_parameters;
        proof_parameters.copy(stream, proof.parameters);
        replacement +=
            emit(projected.name, proof_parameters, proof.proposition, proof.keyword_location, proof.end_line);

        const auto emit_steps = [&](auto&& self, const std::vector<ProofStatement>& statements,
                                    const Generated& parameters) -> void {
            for (const ProofStatement& statement : statements) {
                const auto expression_probe = [&](const source::ByteSpan& span, const source::SourceLocation& at,
                                                  std::vector<std::string>& names, std::string_view kind) {
                    std::string name =
                        options.generated_prefix + std::string(kind) + suffix + "_" + std::to_string(names.size());
                    replacement += line_directive(at.line, proof.keyword_location.file);
                    replacement += emit_expression(name, parameters, span);
                    replacement += line_directive(proof.end_line, proof.keyword_location.file);
                    names.push_back(std::move(name));
                };
                if (statement.kind == ProofStatementKind::Cases || statement.kind == ProofStatementKind::Decompose) {
                    expression_probe(statement.proposition, statement.location, projected.case_names, "case_");
                    const std::string subject_probe = projected.case_names.back();
                    replacement += "static_assert(";
                    replacement += completion_helper;
                    replacement += "<decltype(&";
                    replacement += subject_probe;
                    replacement += ")>::value || true);\n";
                    for (const ProofArm& arm : statement.arms) {
                        // A label that is a C++ expression is resolved by Clang,
                        // like every other expression a proof mentions. A
                        // reserved label names a state that has no expression,
                        // so there is nothing to resolve.
                        if (!arm.keyword_label)
                            expression_probe(arm.label, arm.location, projected.case_names, "case_");
                        Generated scoped = parameters;
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
                    replacement += emit_expression(name, parameters, statement.proposition);
                    replacement += line_directive(proof.end_line, proof.keyword_location.file);
                }
                projected.assumption_names.push_back(std::move(name));
            }
        };
        emit_steps(emit_steps, proof.statements, proof_parameters);

        edits.push_back(generated_edit(proof.range.span, std::move(replacement)));
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

        // Every probe for this function is declared under the function's own
        // template header, so a clause naming a template parameter resolves.
        template_header = verified.template_header.length == 0 ? std::string()
                                                               : std::string(stream.spelling(verified.template_header));
        template_parameters = verified.template_header.length != 0 && !verified.explicit_specialization;
        implicit_object = verified.member && !verified.static_member;

        const std::string suffix = std::to_string(index) + (options.unit_key.empty() ? "" : "_" + options.unit_key);
        std::string_view parameters = stream.spelling(verified.parameters);
        const std::size_t first = parameters.find_first_not_of(" \t\r\n");
        const std::size_t last = parameters.find_last_not_of(" \t\r\n");
        if (first != std::string_view::npos && parameters.substr(first, last - first + 1) == "void") {
            parameters = {};
        }
        const bool has_parameters = parameters.find_first_not_of(" \t\r\n") != std::string_view::npos;
        Generated parameter_list;
        if (!parameters.empty()) {
            parameter_list.copy(stream, verified.parameters);
        }

        Generated result_parameter;
        if (has_parameters) {
            result_parameter += parameter_list;
            result_parameter += ", ";
        }
        const bool void_result =
            options.void_functions.contains(index) || spelled_tokens(stream, verified.return_type) == "void";
        if (void_result) {
            result_parameter = parameter_list;
        } else {
            result_parameter.copy(stream, verified.return_type);
            result_parameter += " result";
        }

        ContractFunctions projected;
        projected.function_index = index;
        projected.postcondition_name = options.generated_prefix + "ensures_" + suffix;

        // Absence of an explicit ensures is legal only when elaboration resolves
        // a refined result. Membership supplies the actual postcondition there.
        Generated replacement;
        if (postcondition != nullptr) {
            replacement = emit(projected.postcondition_name, result_parameter, postcondition->expression,
                               postcondition->location, verified.body_end_line);
        } else {
            replacement += "\n" + line_directive(verified.function_location.line, verified.function_location.file) +
                           declaration_prefix() + "bool " + projected.postcondition_name + "(";
            replacement += result_parameter;
            replacement += ")" + probe_qualifier() + " { return true; }\n";
        }
        for (const Clause* precondition : verified.preconditions()) {
            std::string name = options.generated_prefix + "expects_" + suffix;
            if (!projected.precondition_names.empty()) {
                name += "_" + std::to_string(projected.precondition_names.size());
            }
            replacement +=
                emit(name, parameter_list, precondition->expression, precondition->location, verified.body_end_line);
            projected.precondition_names.push_back(std::move(name));
        }
        // Each component of a `decreases` measure is a function of the
        // parameters returning it, at the type the expression already has
        // (SPEC.md TERMINATION-004). A function template's probes would need
        // forcing at each specialization; its measure is left unprojected, and
        // elaboration refuses it.
        if (const Clause* measure = verified.measure(); measure != nullptr && !templated()) {
            if (detail::contains_formal_syntax(stream, measure->expression)) {
                diagnostics::Diagnostic diagnostic;
                diagnostic.severity = diagnostics::Severity::Error;
                diagnostic.category = diagnostics::Category::UnsupportedSemantics;
                diagnostic.location = measure->location;
                diagnostic.message = "formal syntax in a function measure is not supported yet";
                projection.diagnostics.push_back(std::move(diagnostic));
            }
            for (const MeasureComponent& component : measure_components(stream, *measure)) {
                std::string name = options.generated_prefix + "decreases_" + suffix + "_" +
                                   std::to_string(projected.measure_names.size());
                replacement += "\n";
                replacement += line_directive(component.location.line, component.location.file);
                replacement += declaration_prefix() + "auto " + name + "(";
                replacement += parameter_list;
                replacement += ")" + probe_qualifier() + " { return (";
                replacement += at_written_position(stream, component.expression);
                replacement += "); }\n";
                projected.measure_names.push_back(std::move(name));
            }
        }

        replacement += line_directive(verified.body_end_line, verified.keyword_location.file);
        replacement += std::string(verified.body_end_column - 1, ' ');
        edits.push_back(generated_edit(source::ByteSpan{verified.body_end, 0}, std::move(replacement)));

        // A templated function's probes are templates, and nothing has used
        // them: the specializations that would carry this specialization's
        // contract would never exist. The body names each probe at its own
        // template arguments so that instantiating the function instantiates
        // its contract with it, at the very arguments Clang substituted.
        //
        // The probes are defined after the body, so a declaration of each is
        // emitted before the function for the body to name. The reference
        // itself takes the probe's address into an unused variable: it calls
        // nothing, and the runtime text never sees it (SPEC.md TEMPLATE-001).
        if (!template_header.empty() && verified.body_open != 0) {
            if (const auto names = template_parameter_names(stream, verified.template_header); names.has_value()) {
                // An explicit specialization, `template <>`, declares no
                // parameters. Its arguments are already fixed, so its probes
                // are ordinary functions: there is no primary to specialize,
                // and nothing has to be forced into existence because the
                // declaration itself is the instantiation (SPEC.md
                // TEMPLATE-001).
                const bool specialization = !templated();
                const std::string probe_header = specialization ? std::string{} : template_header + " ";
                std::string declared = "\n";
                declared += line_directive(verified.function_location.line, verified.function_location.file);
                declared += probe_header;
                declared += "bool ";
                declared += projected.postcondition_name;
                declared += "(";
                declared += result_parameter.text;
                declared += ");";
                for (const std::string& precondition : projected.precondition_names) {
                    declared += " ";
                    declared += probe_header;
                    declared += "bool ";
                    declared += precondition;
                    declared += "(";
                    declared += parameters;
                    declared += ");";
                }
                declared += "\n";
                declared += line_directive(verified.keyword_location.line, verified.keyword_location.file);
                declared.append(verified.keyword_location.column - 1, ' ');
                const std::size_t before =
                    verified.template_header.length != 0 ? verified.template_header.offset : verified.keyword.offset;
                edits.push_back(Edit{source::ByteSpan{before, 0}, std::move(declared)});

                if (!specialization) {
                    std::string forced = "\n";
                    forced += line_directive(verified.function_location.line, verified.function_location.file);
                    forced += "[[maybe_unused]] auto " + options.generated_prefix + "force_" + suffix + " = &" +
                              projected.postcondition_name + "<" + *names + ">;";
                    for (std::size_t position = 0; position < projected.precondition_names.size(); ++position) {
                        forced += " [[maybe_unused]] auto " + options.generated_prefix + "force_" + suffix + "_" +
                                  std::to_string(position) + " = &" + projected.precondition_names[position] + "<" +
                                  *names + ">;";
                    }
                    forced += "\n";
                    forced += line_directive(verified.body_open_line, verified.keyword_location.file);
                    forced.append(verified.body_open_column - 1, ' ');
                    edits.push_back(Edit{source::ByteSpan{verified.body_open, 0}, std::move(forced)});
                }
            }
        }
        projection.contract_functions.push_back(std::move(projected));
    }
    template_header.clear();
    template_parameters = false;
    implicit_object = false;

    // A loop's clauses are not C++ either. Each invariant becomes a `bool`
    // declaration at the start of the body, in the scope the loop head sees,
    // and the text after the brace resumes at its own line and column.
    for (std::size_t index = 0; index < syntax.loops.size(); ++index) {
        const LoopSpecification& loop = syntax.loops[index];
        blank(projection.runtime, loop.clause_region);
        edits.push_back(
            Edit{loop.clause_region, projection.runtime.substr(loop.clause_region.offset, loop.clause_region.length)});

        Generated replacement;
        replacement += "\n";
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
            replacement += "[[maybe_unused]] bool " + marker.name + " = (";
            replacement += at_written_position(stream, loop.invariants[position].expression);
            replacement += ");\n";
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
        replacement += line_directive(loop.body_open_line, loop.keyword_location.file);
        replacement += std::string(loop.body_open_column - 1, ' ');
        edits.push_back(generated_edit(source::ByteSpan{loop.body_open, 0}, std::move(replacement)));
    }

    // A claim that a path cannot occur is proof syntax in runtime code. The
    // program keeps its `;`, so an empty statement stands where it was written
    // and whatever statement it was the body of still has one. Clang is given a
    // block at the same point instead, which resolves the evidence's arguments
    // in the scope the statement sees (SPEC.md VERIFIED-023).
    const auto claim_marker = [&options](std::size_t index) {
        return options.generated_prefix + "contradiction_" + std::to_string(index) +
               (options.unit_key.empty() ? "" : "_" + options.unit_key);
    };
    const auto claim_block = [&stream](const std::string& name, const ProofStatement& statement) {
        const std::string& file = statement.location.file;
        Generated block;
        block += "{\n";
        block += line_directive(statement.location.line, file);
        // Starting the declaration at the keyword's column is what makes a
        // diagnostic about the claim point at the `contradiction` written.
        if (statement.location.column > 1) {
            block += std::string(statement.location.column - 1, ' ');
        }
        block += "[[maybe_unused]] bool " + name + " = true;\n";
        for (std::size_t position = 0; position < statement.arguments.size(); ++position) {
            const ProofArgument& argument = statement.arguments[position];
            block += line_directive(argument.location.line, file);
            // `decltype(auto)` over a parenthesized argument binds it as it is,
            // an lvalue by reference, so nothing is copied or converted.
            block += "[[maybe_unused]] decltype(auto) " + name + "_argument_" + std::to_string(position) + " = (";
            block += at_written_position(stream, argument.span);
            block += ");\n";
        }
        block += "}\n";
        return block;
    };
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
        blank(projection.runtime, claim.erased);
        Generated replacement = claim_block(marker.name, claim.statement);
        replacement += line_directive(claim.end_line, claim.statement.location.file);
        replacement += std::string(claim.end_column - 1, ' ');
        edits.push_back(generated_edit(claim.span, std::move(replacement)));
        projection.path_contradictions.push_back(std::move(marker));
    }

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
        replacement += line_directive(split.end_line, file);
        replacement += std::string(split.end_column - 1, ' ');
        edits.push_back(generated_edit(split.span, std::move(replacement)));
    }

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
        reference += "[[maybe_unused]] static auto " + name + " = &" +
                     spelled_tokens(stream, instantiation.id_expression) + ";\n";
        reference += line_directive(instantiation.insertion_line + 1, instantiation.location.file);
        edits.push_back(Edit{source::ByteSpan{instantiation.insertion_offset, 0}, std::move(reference)});
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
    for (const auto& function : syntax.unsafe_functions)
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
        if (end > cursor) {
            projection.segments.push_back(Projection::Segment{cursor, projection.analysis.size(), end - cursor});
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
        if (edit.refinement_index.has_value()) {
            projection.refinement_probes[*edit.refinement_index].alias_offset += projection.analysis.size();
        }
        for (const Projection::Copy& copy : edit.copies) {
            projection.copies.push_back(Projection::Copy{projection.analysis.size() + copy.analysis, copy.original});
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
