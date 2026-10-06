#include "cppl/frontend/projection.hpp"

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/projection.hpp"
#include "formal_projection.hpp"
#include "projector.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend {

using detail::projector::at_written_position;
using detail::projector::blank;
using detail::projector::directive_at;
using detail::projector::directives_within;
using detail::projector::Edit;
using detail::projector::formal_scopes;
using detail::projector::FormalScopes;
using detail::projector::Generated;
using detail::projector::Projector;
using detail::projector::refuse_misplaced_directives;
using detail::projector::resume_at;
using detail::projector::spelled_indices;
using detail::projector::spelled_tokens;

namespace {

using detail::line_directive;

} // namespace

namespace detail::projector {

// The directive line of `stream` holding byte `offset`, if one does.
const Directive* directive_at(const TokenStream& stream, std::size_t offset) {
    const std::vector<Directive>& directives = stream.directives();
    const auto found = std::ranges::upper_bound(directives, offset, {},
                                                [](const Directive& directive) { return directive.span.offset; });
    if (found == directives.begin()) {
        return nullptr;
    }
    const Directive& candidate = *std::prev(found);
    return offset < candidate.span.end() ? &candidate : nullptr;
}

} // namespace detail::projector

namespace {

// `lowered`, the canonical C++ a runtime-bearing construct means, standing where
// `span` was written: it occupies the span's first line, and every later line
// of the span stays a line, holding nothing but the directive written on it if
// it is a directive line (blank). The span's last line is padded with spaces to
// the length it had, so whatever follows the span on that line keeps its column
// (SPEC.md ERASE-018). A lowering longer than a span written on one line cannot
// be padded, and is left longer (`lowering_moves_columns`).
std::string in_place_of(const TokenStream& stream, const source::ByteSpan& span, std::string_view lowered) {
    std::string text(lowered);
    const std::string_view written = stream.spelling(span);
    std::size_t last_line = 0; // where the span's last line starts within it
    for (std::size_t at = 0; at < written.size(); ++at) {
        if (written[at] != '\n') {
            continue;
        }
        text += '\n';
        last_line = at + 1;
        const std::size_t line = span.offset + at + 1;
        if (const Directive* directive = directive_at(stream, line);
            directive != nullptr && directive->span.offset == line && directive->span.end() <= span.end()) {
            text += stream.spelling(directive->span);
        }
    }
    const std::size_t written_last = written.size() - last_line;
    const std::size_t lowered_last = text.size() - (last_line == 0 ? 0 : text.rfind('\n') + 1);
    if (lowered_last < written_last) {
        text.append(written_last - lowered_last, ' ');
    }
    return text;
}

} // namespace

namespace detail::projector {

// Whether `lowered`, standing where `span` was written, would move text written
// after the span on its last line: it is longer than the span's last line, and
// something other than spaces follows the span there.
bool lowering_moves_columns(const TokenStream& stream, const source::ByteSpan& span, const std::string& lowered) {
    const std::string_view text = stream.text();
    const std::string_view written = stream.spelling(span);
    const std::size_t newline = written.rfind('\n');
    const std::size_t written_last = newline == std::string_view::npos ? written.size() : written.size() - newline - 1;
    const std::size_t lowered_newline = lowered.rfind('\n');
    const std::size_t lowered_last =
        lowered_newline == std::string::npos ? lowered.size() : lowered.size() - lowered_newline - 1;
    if (lowered_last <= written_last) {
        return false;
    }
    const std::size_t line_end = std::min(text.find('\n', span.end()), text.size());
    const std::string_view after = text.substr(span.end(), line_end - span.end());
    return after.find_first_not_of(" \t\r") != std::string_view::npos;
}

// The directives other than line markers written within `span`, each on a line
// of its own, for an analysis text that replaces the span with generated C++:
// what they do to the C++ after the span, they do there too. The line markers
// need no copy, since the generated text states its own lines.
std::string directives_within(const TokenStream& stream, const source::ByteSpan& span) {
    std::string text;
    for (const Directive& directive : stream.directives()) {
        if (directive.span.offset >= span.offset && directive.span.end() <= span.end() && !directive.line_marker) {
            text += "\n";
            text += stream.spelling(directive.span);
            text += "\n";
        }
    }
    return text;
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

// A line directive and indentation after which the analysis text resumes, on a
// line of its own, with byte `offset` of the scanned text at the line and the
// column it has there. That is where the runtime program has it too, since
// erasure moves no byte to another line or column, so C++ that observes a
// position after generated text -- `__builtin_LINE()`, `__builtin_COLUMN()`,
// `std::source_location` -- observes the position the program observes, in a
// template argument as anywhere else (SPEC.md ERASE-018).
//
// The column is the byte's column in the scanned text, which is what Clang
// counts in both programs, and never the column its author wrote it at: the
// preprocessor writes a run of spaces between tokens as one.
std::string resume_at(const TokenStream& stream, std::size_t offset) {
    const std::string_view text = stream.text();
    offset = std::min(offset, text.size());
    const std::size_t line_start = offset == 0 ? 0 : text.rfind('\n', offset - 1) + 1; // npos + 1 is 0
    const std::vector<Token>& tokens = stream.tokens();
    // The presumed line is a token's: the last one ending at or before the byte
    // on its line, or else the first one starting at or after it there.
    const Token* anchor = nullptr;
    std::uint32_t line = 0;
    const auto after =
        std::ranges::lower_bound(tokens, offset, {}, [](const Token& token) { return token.span.offset; });
    if (after != tokens.begin()) {
        const Token& before = *std::prev(after);
        if (before.kind != TokenKind::EndOfFile && before.span.end() <= offset && before.span.end() >= line_start) {
            anchor = &before;
            line = before.line + static_cast<std::uint32_t>(std::ranges::count(before.text, '\n'));
        }
    }
    if (anchor == nullptr && after != tokens.end() && after->kind != TokenKind::EndOfFile &&
        text.substr(offset, after->span.offset - offset).find('\n') == std::string_view::npos) {
        anchor = &*after;
        line = after->line;
    }
    if (anchor == nullptr) {
        return {};
    }
    std::string resumed = "\n";
    resumed += line_directive(line, stream.location_of(*anchor).file);
    resumed.append(offset - line_start, ' ');
    return resumed;
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

} // namespace detail::projector

std::string canonical_lowering(const TokenStream& stream, const RefinementType& refinement) {
    std::string text;
    if (refinement.indexed) {
        text += "template <" + spelled_indices(stream, refinement) + "> ";
    }
    text += "using " + refinement.name + " = " + spelled_tokens(stream, refinement.base) + ";";
    // A validation expression of this unit tests values against the predicate
    // at run time, so the program keeps the predicate as the body of the one
    // function every such expression calls, stated where the declaration stands
    // so its names mean what they mean there (SPEC.md RUNTIMECHECK-021).
    if (!refinement.validator.empty()) {
        text += " [[maybe_unused]] static inline bool " + refinement.validator + "(" +
                spelled_tokens(stream, refinement.base) + " self) { return static_cast<bool>(" +
                spelled_tokens(stream, refinement.predicate) + "); }";
    }
    // Every line of the declaration stays a line of the program, so nothing
    // below it moves, and every directive written in it stays where it was.
    return in_place_of(stream, refinement.range.span, text);
}

std::string lowered_validation(const TokenStream& stream, const Syntax& syntax,
                               const ValidationExpression& validation) {
    // A `validate<R>` written across lines keeps every line.
    return in_place_of(stream, validation.callee, syntax.refinement_types[validation.refinement_index].validator);
}

std::string erased_split(const TokenStream& stream, const PathCaseSplit& split) {
    std::string text(stream.spelling(split.span));
    blank(text, source::ByteSpan{0, text.size()}, stream, split.span.offset);
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
    Projector projector(stream, syntax, options);
    projector.project_markers();
    projector.project_refinements();
    projector.project_validations();

    // Every Law and proof is projected into the formal namespace of the
    // namespace it is written in, and nothing else is (formal_scopes).
    const FormalScopes formal = formal_scopes(stream, syntax, options);
    projector.project_laws(formal);
    projector.project_proofs(formal);
    projector.project_contracts();
    projector.project_loops();
    projector.project_path_claims();
    projector.project_path_splits();
    projector.project_explicit_instantiations();
    projector.assemble();

    refuse_misplaced_directives(stream, syntax, projector.projection.diagnostics);
    return std::move(projector.projection);
}

std::optional<std::size_t> Projection::declaration_offset(std::size_t original) const {
    for (const auto& declaration : declaration_offsets) {
        if (declaration.original == original)
            return declaration.analysis;
    }
    return std::nullopt;
}

Generated Projector::emit(std::string_view name, const Generated& parameters, const source::ByteSpan& expression,
                          const source::SourceLocation& begin, std::uint32_t end_line, std::size_t* name_offset,
                          std::string* proposition_name) {
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
}

Generated Projector::in_formal_scope(const std::string& opening, const Generated& declarations,
                                     const source::ByteSpan& span) const {
    Generated scoped;
    scoped += opening;
    scoped += declarations;
    scoped += "}\n";
    scoped += directives_within(stream, span);
    scoped += resume_at(stream, span.end());
    return scoped;
}

Generated Projector::claim_block(const std::string& name, const ProofStatement& statement) const {
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
}

void Projector::project_markers() {
    for (const PureMarker& marker : syntax.pure_markers) {
        blank(projection.runtime, marker.keyword, stream);
        edits.push_back(Edit{marker.keyword, std::string(marker.keyword.length, ' ')});
    }

    // `unsafe` is a marker: the word leaves both texts and the declaration or
    // the block it marks stays ordinary C++ (SPEC.md ERASE-003, Annex M).
    for (const UnsafeFunction& function : syntax.unsafe_functions) {
        blank(projection.runtime, function.keyword, stream);
        edits.push_back(Edit{function.keyword, std::string(function.keyword.length, ' ')});
    }
    // A block's region starts at a declaration only Clang sees, just inside its
    // `{`, written at the position of the word it replaces, so the region's
    // provenance is where the author wrote `unsafe`.
    for (std::size_t index = 0; index < syntax.unsafe_blocks.size(); ++index) {
        const UnsafeBlock& block = syntax.unsafe_blocks[index];
        blank(projection.runtime, block.keyword, stream);
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
        inserted += marker.name + " = true;";
        inserted += resume_at(stream, block.body_open);
        edits.push_back(Edit{source::ByteSpan{block.body_open, 0}, std::move(inserted)});
        projection.unsafe_blocks.push_back(std::move(marker));
    }

    // A ghost declaration leaves the program whole (SPEC.md GHOST-001,
    // ERASE-011). Clang still sees it, after a declaration only Clang sees,
    // named where `ghost` was written, which tells the body lowering that the
    // declaration after it is ghost state.
    for (std::size_t index = 0; index < syntax.ghost_declarations.size(); ++index) {
        const GhostDeclaration& ghost = syntax.ghost_declarations[index];
        blank(projection.runtime, ghost.erased, stream);
        const std::string name = options.generated_prefix + "ghost_" + std::to_string(index) +
                                 (options.unit_key.empty() ? "" : "_" + options.unit_key);
        const std::size_t column = ghost.location.column > 1 ? ghost.location.column - 1 : 0;
        std::string inserted = "\n[[maybe_unused]] bool\n";
        inserted += line_directive(ghost.location.line, ghost.location.file);
        inserted += std::string(column, ' ') + name + " = true;";
        inserted += resume_at(stream, ghost.keyword.end());
        edits.push_back(Edit{ghost.keyword, std::move(inserted)});
    }
}

void Projector::assemble() {
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

    // What the program run lacks: every run between the copied segments, and
    // each ghost declaration where a segment copied it (SPEC.md ERASE-019).
    std::vector<source::ByteSpan> proof_only;
    std::size_t copied_up_to = 0;
    for (const Projection::Segment& segment : projection.segments) {
        if (segment.analysis > copied_up_to) {
            proof_only.push_back(source::ByteSpan{copied_up_to, segment.analysis - copied_up_to});
        }
        copied_up_to = segment.analysis + segment.length;
    }
    if (projection.analysis.size() > copied_up_to) {
        proof_only.push_back(source::ByteSpan{copied_up_to, projection.analysis.size() - copied_up_to});
    }
    for (const GhostDeclaration& ghost : syntax.ghost_declarations) {
        for (const Projection::Segment& segment : projection.segments) {
            const std::size_t begin = std::max(ghost.erased.offset, segment.original);
            const std::size_t end = std::min(ghost.erased.end(), segment.original + segment.length);
            if (begin < end) {
                proof_only.push_back(source::ByteSpan{segment.analysis + (begin - segment.original), end - begin});
            }
        }
    }
    std::ranges::sort(proof_only, {}, &source::ByteSpan::offset);
    for (const source::ByteSpan& span : proof_only) {
        if (!projection.proof_only.empty() && span.offset <= projection.proof_only.back().end()) {
            source::ByteSpan& last = projection.proof_only.back();
            last.length = std::max(last.end(), span.end()) - last.offset;
        } else {
            projection.proof_only.push_back(span);
        }
    }
}

} // namespace cppl::frontend
