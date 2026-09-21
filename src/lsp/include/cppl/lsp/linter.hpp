#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/protocol.hpp"

#include <string_view>
#include <vector>

namespace cppl::lsp {

class PositionMapper;

// Produces syntax-aware C++L lint diagnostics
class Linter {
  public:
    // Lint the given parsed syntax and return LSP diagnostics
    [[nodiscard]] std::vector<Diagnostic> lint(const frontend::TokenStream& tokens, const frontend::Syntax& syntax,
                                               const std::vector<diagnostics::Diagnostic>& parse_diagnostics,
                                               const PositionMapper& mapper) const;

  private:
    void lint_laws(const std::vector<frontend::LawDeclaration>& laws, std::vector<Diagnostic>& out,
                   const PositionMapper& mapper) const;

    void lint_proofs(const std::vector<frontend::ProofDeclaration>& proofs, std::vector<Diagnostic>& out,
                     const PositionMapper& mapper) const;

    void lint_verified_functions(const std::vector<frontend::VerifiedFunction>& functions, std::vector<Diagnostic>& out,
                                 const PositionMapper& mapper) const;

    void lint_refinement_types(const std::vector<frontend::RefinementType>& refinements, std::vector<Diagnostic>& out,
                               const PositionMapper& mapper) const;

    void lint_loops(const std::vector<frontend::LoopSpecification>& loops, std::vector<Diagnostic>& out,
                    const PositionMapper& mapper) const;

    void check_clause_validity(const std::vector<frontend::Clause>& clauses, const std::string& context,
                               std::vector<Diagnostic>& out, const PositionMapper& mapper) const;

    void check_proof_statements(const std::vector<frontend::ProofStatement>& statements, std::vector<Diagnostic>& out,
                                const PositionMapper& mapper) const;

  public:
    // Converts one compiler diagnostic (from any pipeline stage: C++L
    // syntax, Clang C++ semantics, elaboration, obligations) to its LSP
    // form. Exposed so a caller merging diagnostics from multiple engine
    // runs (Server::publish_diagnostics) can convert them the same way
    // lint() converts C++L syntax diagnostics, rather than re-deriving the
    // severity/code/location mapping.
    [[nodiscard]] Diagnostic convert_diagnostic(const diagnostics::Diagnostic& diag,
                                                const PositionMapper& mapper) const;
};

} // namespace cppl::lsp
