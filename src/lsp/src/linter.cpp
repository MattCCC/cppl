#include "cppl/lsp/linter.hpp"

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/diagnostic_codes.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/uri.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

// Where a header's diagnostic is shown: the whole `#include` line in the
// document that brought the header in, or the document's start when no
// include of it is known.
Range include_range(const std::string& file, const PositionMapper& mapper, const PublishedDocument& document) {
    if (document.tokens != nullptr) {
        std::string current = file;
        // Each step moves to a file entered earlier, so this ends; the bound
        // only guards against a stream that says otherwise.
        for (std::size_t step = 0; step < document.tokens->files().size(); ++step) {
            const std::optional<source::SourceLocation> site = document.tokens->included_at(current);
            if (!site.has_value()) {
                break;
            }
            if (site->file == document.path) {
                const Position start{site->line - 1, 0};
                const Position end =
                    mapper.byte_offset_to_position(mapper.position_to_byte_offset(Position{start.line, UINT32_MAX}));
                return Range{start, end};
            }
            current = site->file;
        }
    }
    return Range{};
}

// A construct's range in the document. Its end is its last token's written
// position plus that token's length, never its byte span, which counts bytes of
// whatever text the syntax was recognized from: for the compile's syntax, the
// preprocessed unit rather than the document.
Range construct_range(const source::SourceRange& range, const PositionMapper& mapper,
                      const PublishedDocument& document) {
    const Position start = mapper.source_location_to_position(range.begin);
    if (document.tokens == nullptr) {
        return Range{start, start};
    }
    const std::vector<frontend::Token>& tokens = document.tokens->tokens();
    const auto after = std::ranges::lower_bound(tokens, range.span.end(), {},
                                                [](const frontend::Token& token) { return token.span.offset; });
    if (after == tokens.begin() || std::prev(after)->span.offset < range.span.offset) {
        return Range{start, start};
    }
    const frontend::Token& last = *std::prev(after);
    source::SourceLocation end = document.tokens->location_of(last);
    if (!document.holds(end)) {
        return Range{start, start};
    }
    end.column += static_cast<std::uint32_t>(last.span.length);
    return Range{start, mapper.source_location_to_position(end)};
}

// A location in another file, whose text is not at hand: its byte column is
// the best character there is.
Position elsewhere(const source::SourceLocation& location) {
    return Position{location.line > 0 ? location.line - 1 : 0, location.column > 0 ? location.column - 1 : 0};
}

} // namespace

std::vector<Diagnostic> Linter::lint(const frontend::TokenStream& tokens, const frontend::Syntax& syntax,
                                     const std::vector<diagnostics::Diagnostic>& parse_diagnostics,
                                     const PositionMapper& mapper, const PublishedDocument& document) const {
    std::vector<Diagnostic> diagnostics;
    PublishedDocument read = document;
    read.tokens = &tokens;

    // Convert frontend diagnostics first
    diagnostics.reserve(parse_diagnostics.size());
    for (const auto& diag : parse_diagnostics) {
        diagnostics.push_back(convert_diagnostic(diag, mapper, read));
    }

    // Lint each construct type. A header's constructs are the header's to
    // report, where it is itself open.
    lint_laws(syntax.laws, diagnostics, mapper, read);
    lint_proofs(syntax.proofs, diagnostics, mapper, read);
    lint_verified_functions(syntax.verified_functions, diagnostics, mapper, read);
    lint_refinement_types(syntax.refinement_types, diagnostics, mapper, read);
    lint_loops(syntax.loops, diagnostics, mapper, read);

    return diagnostics;
}

void Linter::lint_laws(const std::vector<frontend::LawDeclaration>& laws, std::vector<Diagnostic>& out,
                       const PositionMapper& mapper, const PublishedDocument& document) const {
    for (const auto& law : laws) {
        if (!document.holds(law.keyword_location)) {
            continue;
        }
        // Check for exactly one proves clause
        std::size_t proves_count = 0;
        for (const auto& clause : law.clauses) {
            if (clause.kind == frontend::ClauseKind::Proves) {
                ++proves_count;
            }
        }

        if (proves_count == 0) {
            Diagnostic diag;
            diag.range = construct_range(law.range, mapper, document);
            diag.severity = DiagnosticSeverity::Error;
            diag.code = std::string(diagnostic_codes::law_missing_proves_clause);
            diag.message = "law '" + law.name + "' must have exactly one 'proves' clause";
            out.push_back(std::move(diag));
        } else if (proves_count > 1) {
            Diagnostic diag;
            diag.range = construct_range(law.range, mapper, document);
            diag.severity = DiagnosticSeverity::Error;
            diag.code = std::string(diagnostic_codes::law_multiple_proves);
            diag.message =
                "law '" + law.name + "' has " + std::to_string(proves_count) + " 'proves' clauses; only one is allowed";
            out.push_back(std::move(diag));
        }

        // Check clause validity
        check_clause_validity(law.clauses, "law '" + law.name + "'", out, mapper);
    }
}

void Linter::lint_proofs(const std::vector<frontend::ProofDeclaration>& proofs, std::vector<Diagnostic>& out,
                         const PositionMapper& mapper, const PublishedDocument& document) const {
    for (const auto& proof : proofs) {
        if (!document.holds(proof.keyword_location)) {
            continue;
        }
        // Check that proves clause exists and is non-empty
        if (proof.proposition.length == 0) {
            Diagnostic diag;
            diag.range = construct_range(proof.range, mapper, document);
            diag.severity = DiagnosticSeverity::Error;
            diag.code = std::string(diagnostic_codes::proof_missing_proves);
            diag.message = "proof '" + proof.name + "' must have a 'proves' clause with a proposition";
            out.push_back(std::move(diag));
        }

        // Check that body is non-empty
        if (proof.statements.empty()) {
            Diagnostic diag;
            diag.range = construct_range(proof.range, mapper, document);
            diag.severity = DiagnosticSeverity::Warning;
            diag.code = std::string(diagnostic_codes::proof_empty_body);
            diag.message = "proof '" + proof.name + "' has an empty body";
            out.push_back(std::move(diag));
        }

        // Check proof statements
        check_proof_statements(proof.statements, out, mapper);
    }
}

void Linter::lint_verified_functions(const std::vector<frontend::VerifiedFunction>& functions,
                                     std::vector<Diagnostic>& out, const PositionMapper& mapper, const PublishedDocument& document) const {
    for (const auto& func : functions) {
        if (!document.holds(func.keyword_location)) {
            continue;
        }
        // A refined return supplies a postcondition without an ensures clause.
        // Only shared semantic analysis can decide that from the resolved type.
        // Check clause validity
        check_clause_validity(func.clauses, "verified function '" + func.function_name + "'", out, mapper);
    }
}

void Linter::lint_refinement_types(const std::vector<frontend::RefinementType>& refinements,
                                   std::vector<Diagnostic>& out, const PositionMapper& mapper, const PublishedDocument& document) const {
    for (const auto& refinement : refinements) {
        if (!document.holds(refinement.keyword_location)) {
            continue;
        }
        // Check that base type is present
        if (refinement.base.length == 0) {
            Diagnostic diag;
            diag.range = construct_range(refinement.range, mapper, document);
            diag.severity = DiagnosticSeverity::Error;
            diag.code = std::string(diagnostic_codes::refinement_invalid_base);
            diag.message = "refinement type '" + refinement.name + "' must have a base type";
            out.push_back(std::move(diag));
        }

        // Check that predicate is present
        if (refinement.predicate.length == 0) {
            Diagnostic diag;
            diag.range = construct_range(refinement.range, mapper, document);
            diag.severity = DiagnosticSeverity::Error;
            diag.code = std::string(diagnostic_codes::refinement_missing_predicate);
            diag.message = "refinement type '" + refinement.name + "' must have a 'where' clause with a predicate";
            out.push_back(std::move(diag));
        }
    }
}

void Linter::lint_loops(const std::vector<frontend::LoopSpecification>& loops, std::vector<Diagnostic>& out,
                        const PositionMapper& mapper, const PublishedDocument& document) const {
    for (const auto& loop : loops) {
        if (!document.holds(loop.keyword_location)) {
            continue;
        }
        // Check that invariants are non-empty
        if (loop.invariants.empty()) {
            Position keyword_pos = mapper.source_location_to_position(loop.keyword_location);
            Diagnostic diag;
            diag.range = Range{keyword_pos, keyword_pos};
            diag.severity = DiagnosticSeverity::Warning;
            diag.code = std::string(diagnostic_codes::contract_invalid_placement);
            diag.message = "loop should have at least one 'invariant' clause";
            out.push_back(std::move(diag));
        }

        // Check clause validity
        check_clause_validity(loop.invariants, "loop", out, mapper);
    }
}

void Linter::check_clause_validity(const std::vector<frontend::Clause>& clauses, const std::string& context,
                                   std::vector<Diagnostic>& out, const PositionMapper& mapper) const {
    for (const auto& clause : clauses) {
        // Check for empty clauses
        if (clause.expression.length == 0) {
            Position loc_pos = mapper.source_location_to_position(clause.location);
            Diagnostic diag;
            diag.range = Range{loc_pos, loc_pos};
            diag.severity = DiagnosticSeverity::Error;
            diag.code = std::string(diagnostic_codes::contract_empty_clause);
            diag.message = context + " has an empty '" + frontend::describe(clause.kind) + "' clause";
            out.push_back(std::move(diag));
        }
    }
}

void Linter::check_proof_statements(const std::vector<frontend::ProofStatement>& statements,
                                    std::vector<Diagnostic>& out, const PositionMapper& mapper) const {
    for (const auto& stmt : statements) {
        // Check statement-specific requirements
        switch (stmt.kind) {
            case frontend::ProofStatementKind::Exact:
            case frontend::ProofStatementKind::Apply:
            case frontend::ProofStatementKind::Contradiction:
                if (stmt.reference.empty()) {
                    Position loc_pos = mapper.source_location_to_position(stmt.location);
                    Diagnostic diag;
                    diag.range = Range{loc_pos, loc_pos};
                    diag.severity = DiagnosticSeverity::Error;
                    diag.code = std::string(diagnostic_codes::proof_malformed_statement);
                    diag.message = "'" + frontend::describe(stmt.kind) + "' statement requires a proof reference";
                    out.push_back(std::move(diag));
                }
                break;

            case frontend::ProofStatementKind::Assume:
                if (stmt.reference.empty()) {
                    Position loc_pos = mapper.source_location_to_position(stmt.location);
                    Diagnostic diag;
                    diag.range = Range{loc_pos, loc_pos};
                    diag.severity = DiagnosticSeverity::Error;
                    diag.code = std::string(diagnostic_codes::proof_malformed_statement);
                    diag.message = "'assume' statement requires a binding name";
                    out.push_back(std::move(diag));
                }
                if (stmt.proposition.length == 0) {
                    Position loc_pos = mapper.source_location_to_position(stmt.proposition_location);
                    Diagnostic diag;
                    diag.range = Range{loc_pos, loc_pos};
                    diag.severity = DiagnosticSeverity::Error;
                    diag.code = std::string(diagnostic_codes::proof_malformed_statement);
                    diag.message = "'assume' statement requires a proposition";
                    out.push_back(std::move(diag));
                }
                break;

            case frontend::ProofStatementKind::Cases:
            case frontend::ProofStatementKind::Decompose:
                // `decompose` is `cases` over a product's components rather
                // than a sum's alternatives (SPEC.md, ProductDecomposition):
                // same arm/statement shape, so the same structural checks
                // apply (frontend::describe(stmt.kind) names whichever
                // keyword was actually written).
                if (stmt.arms.empty()) {
                    Position loc_pos = mapper.source_location_to_position(stmt.location);
                    Diagnostic diag;
                    diag.range = Range{loc_pos, loc_pos};
                    diag.severity = DiagnosticSeverity::Warning;
                    diag.code = std::string(diagnostic_codes::proof_malformed_statement);
                    diag.message = "'" + frontend::describe(stmt.kind) + "' statement should have at least one case";
                    out.push_back(std::move(diag));
                }
                // Recursively check nested statements
                for (const auto& arm : stmt.arms) {
                    check_proof_statements(arm.statements, out, mapper);
                }
                break;

            case frontend::ProofStatementKind::Induction:
                // The short form ('induction n;') legitimately has no arms -
                // it requests automation for every case - so an empty
                // `arms` list is not itself malformed here, unlike
                // `cases`/`decompose`. Recurse into whichever arms were
                // written so nested statements are still checked.
                for (const auto& arm : stmt.arms) {
                    check_proof_statements(arm.statements, out, mapper);
                }
                break;

            case frontend::ProofStatementKind::Reflexivity:
            case frontend::ProofStatementKind::Rewrite:
                // These have no additional requirements
                break;
        }
    }
}

Diagnostic Linter::convert_diagnostic(const diagnostics::Diagnostic& diag, const PositionMapper& mapper,
                                      const PublishedDocument& document) const {
    Diagnostic lsp_diag;

    // Map severity
    switch (diag.severity) {
        case diagnostics::Severity::Error:
            lsp_diag.severity = DiagnosticSeverity::Error;
            break;
        case diagnostics::Severity::Warning:
            lsp_diag.severity = DiagnosticSeverity::Warning;
            break;
        case diagnostics::Severity::Note:
            lsp_diag.severity = DiagnosticSeverity::Information;
            break;
    }

    // Map category to code
    switch (diag.category) {
        case diagnostics::Category::CpplSyntax:
            lsp_diag.code = std::string(diagnostic_codes::syntax_unexpected_construct);
            break;
        case diagnostics::Category::CppSemantic:
            lsp_diag.code = "cppl.cpp.semantic";
            break;
        case diagnostics::Category::Elaboration:
            lsp_diag.code = "cppl.elaboration";
            break;
        case diagnostics::Category::UnsupportedSemantics:
            lsp_diag.code = "cppl.unsupported";
            break;
        case diagnostics::Category::ProofFailure:
            lsp_diag.code = "cppl.proof.failure";
            break;
        case diagnostics::Category::KernelRejection:
            lsp_diag.code = "cppl.kernel.rejection";
            break;
        case diagnostics::Category::Policy:
            lsp_diag.code = "cppl.policy";
            break;
        case diagnostics::Category::Style:
            lsp_diag.code = "cppl.style";
            break;
        case diagnostics::Category::Internal:
            lsp_diag.code = "cppl.internal";
            break;
    }

    lsp_diag.message = diag.message;

    if (document.holds(diag.location)) {
        const Position pos = mapper.source_location_to_position(diag.location);
        lsp_diag.range = Range{pos, pos};
    } else {
        lsp_diag.range = include_range(diag.location.file, mapper, document);
        lsp_diag.message = "in included file: " + diag.message;
        DiagnosticRelatedInformation where;
        where.message = diag.message;
        where.location.uri = path_to_uri(diag.location.file);
        where.location.range = Range{elsewhere(diag.location), elsewhere(diag.location)};
        lsp_diag.relatedInformation.push_back(std::move(where));
    }

    for (const auto& note : diag.notes) {
        DiagnosticRelatedInformation related;
        related.message = note.message;
        if (document.holds(note.location)) {
            const Position pos = mapper.source_location_to_position(note.location);
            related.location.uri = document.uri;
            related.location.range = Range{pos, pos};
        } else {
            related.location.uri = path_to_uri(note.location.file);
            related.location.range = Range{elsewhere(note.location), elsewhere(note.location)};
        }
        lsp_diag.relatedInformation.push_back(std::move(related));
    }

    return lsp_diag;
}

} // namespace cppl::lsp
