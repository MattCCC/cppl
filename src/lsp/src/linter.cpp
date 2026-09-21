#include "cppl/lsp/linter.hpp"

#include "cppl/lsp/diagnostic_codes.hpp"
#include "cppl/lsp/position.hpp"

#include <unordered_set>

namespace cppl::lsp {

std::vector<Diagnostic> Linter::lint(const frontend::TokenStream& tokens, const frontend::Syntax& syntax,
                                     const std::vector<diagnostics::Diagnostic>& parse_diagnostics,
                                     const PositionMapper& mapper) const {
    std::vector<Diagnostic> diagnostics;

    // Convert frontend diagnostics first
    diagnostics.reserve(parse_diagnostics.size());
    for (const auto& diag : parse_diagnostics) {
        diagnostics.push_back(convert_diagnostic(diag, mapper));
    }

    // Lint each construct type
    lint_laws(syntax.laws, diagnostics, mapper);
    lint_proofs(syntax.proofs, diagnostics, mapper);
    lint_verified_functions(syntax.verified_functions, diagnostics, mapper);
    lint_refinement_types(syntax.refinement_types, diagnostics, mapper);
    lint_loops(syntax.loops, diagnostics, mapper);

    return diagnostics;
}

void Linter::lint_laws(const std::vector<frontend::LawDeclaration>& laws, std::vector<Diagnostic>& out,
                       const PositionMapper& mapper) const {
    for (const auto& law : laws) {
        // Check for exactly one proves clause
        std::size_t proves_count = 0;
        for (const auto& clause : law.clauses) {
            if (clause.kind == frontend::ClauseKind::Proves) {
                ++proves_count;
            }
        }

        if (proves_count == 0) {
            Diagnostic diag;
            diag.range = mapper.source_range_to_range(law.range);
            diag.severity = DiagnosticSeverity::Error;
            diag.code = std::string(diagnostic_codes::law_missing_proves_clause);
            diag.message = "law '" + law.name + "' must have exactly one 'proves' clause";
            out.push_back(std::move(diag));
        } else if (proves_count > 1) {
            Diagnostic diag;
            diag.range = mapper.source_range_to_range(law.range);
            diag.severity = DiagnosticSeverity::Error;
            diag.code = std::string(diagnostic_codes::law_multiple_proves);
            diag.message = "law '" + law.name + "' has " + std::to_string(proves_count) +
                           " 'proves' clauses; only one is allowed";
            out.push_back(std::move(diag));
        }

        // Check clause validity
        check_clause_validity(law.clauses, "law '" + law.name + "'", out, mapper);
    }
}

void Linter::lint_proofs(const std::vector<frontend::ProofDeclaration>& proofs, std::vector<Diagnostic>& out,
                         const PositionMapper& mapper) const {
    for (const auto& proof : proofs) {
        // Check that proves clause exists and is non-empty
        if (proof.proposition.length == 0) {
            Diagnostic diag;
            diag.range = mapper.source_range_to_range(proof.range);
            diag.severity = DiagnosticSeverity::Error;
            diag.code = std::string(diagnostic_codes::proof_missing_proves);
            diag.message = "proof '" + proof.name + "' must have a 'proves' clause with a proposition";
            out.push_back(std::move(diag));
        }

        // Check that body is non-empty
        if (proof.statements.empty()) {
            Diagnostic diag;
            diag.range = mapper.source_range_to_range(proof.range);
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
                                     std::vector<Diagnostic>& out, const PositionMapper& mapper) const {
    for (const auto& func : functions) {
        // A refined return supplies a postcondition without an ensures clause.
        // Only shared semantic analysis can decide that from the resolved type.
        // Check clause validity
        check_clause_validity(func.clauses, "verified function '" + func.function_name + "'", out, mapper);
    }
}

void Linter::lint_refinement_types(const std::vector<frontend::RefinementType>& refinements,
                                   std::vector<Diagnostic>& out, const PositionMapper& mapper) const {
    for (const auto& refinement : refinements) {
        // Check that base type is present
        if (refinement.base.length == 0) {
            Diagnostic diag;
            diag.range = mapper.source_range_to_range(refinement.range);
            diag.severity = DiagnosticSeverity::Error;
            diag.code = std::string(diagnostic_codes::refinement_invalid_base);
            diag.message = "refinement type '" + refinement.name + "' must have a base type";
            out.push_back(std::move(diag));
        }

        // Check that predicate is present
        if (refinement.predicate.length == 0) {
            Diagnostic diag;
            diag.range = mapper.source_range_to_range(refinement.range);
            diag.severity = DiagnosticSeverity::Error;
            diag.code = std::string(diagnostic_codes::refinement_missing_predicate);
            diag.message = "refinement type '" + refinement.name + "' must have a 'where' clause with a predicate";
            out.push_back(std::move(diag));
        }
    }
}

void Linter::lint_loops(const std::vector<frontend::LoopSpecification>& loops, std::vector<Diagnostic>& out,
                        const PositionMapper& mapper) const {
    for (const auto& loop : loops) {
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

            case frontend::ProofStatementKind::Reflexivity:
            case frontend::ProofStatementKind::Rewrite:
                // These have no additional requirements
                break;
        }
    }
}

Diagnostic Linter::convert_diagnostic(const diagnostics::Diagnostic& diag, const PositionMapper& mapper) const {
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

    // Convert location to range
    Position pos = mapper.source_location_to_position(diag.location);
    lsp_diag.range = Range{pos, pos};

    // Convert notes to related information
    for (const auto& note : diag.notes) {
        DiagnosticRelatedInformation related;
        related.message = note.message;
        Position note_pos = mapper.source_location_to_position(note.location);
        related.location.uri = diag.location.file;
        related.location.range = Range{note_pos, note_pos};
        lsp_diag.relatedInformation.push_back(std::move(related));
    }

    return lsp_diag;
}

} // namespace cppl::lsp
