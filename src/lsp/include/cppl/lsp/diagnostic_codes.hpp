#pragma once

#include <string_view>

namespace cppl::lsp {

// Stable diagnostic codes for C++L linting
// These identifiers are part of the LSP contract and must remain stable
namespace diagnostic_codes {

// Syntax errors
inline constexpr std::string_view syntax_malformed_law = "cppl.syntax.malformed-law";
inline constexpr std::string_view syntax_malformed_proof = "cppl.syntax.malformed-proof";
inline constexpr std::string_view syntax_malformed_verified = "cppl.syntax.malformed-verified";
inline constexpr std::string_view syntax_malformed_refinement = "cppl.syntax.malformed-refinement";
inline constexpr std::string_view syntax_malformed_clause = "cppl.syntax.malformed-clause";
inline constexpr std::string_view syntax_unexpected_construct = "cppl.syntax.unexpected-construct";

// Contract errors
inline constexpr std::string_view contract_missing_ensures = "cppl.contract.missing-ensures";
inline constexpr std::string_view contract_duplicate_clause = "cppl.contract.duplicate-clause";
inline constexpr std::string_view contract_invalid_placement = "cppl.contract.invalid-placement";
inline constexpr std::string_view contract_empty_clause = "cppl.contract.empty-clause";

// Proof errors (structural only, not semantic)
inline constexpr std::string_view proof_missing_proves = "cppl.proof.missing-proves";
inline constexpr std::string_view proof_empty_body = "cppl.proof.empty-body";
inline constexpr std::string_view proof_invalid_statement = "cppl.proof.invalid-statement";
inline constexpr std::string_view proof_malformed_statement = "cppl.proof.malformed-statement";

// Law errors
inline constexpr std::string_view law_multiple_ensures = "cppl.law.multiple-ensures";
inline constexpr std::string_view law_missing_ensures_clause = "cppl.law.missing-ensures";
inline constexpr std::string_view law_invalid_clause_order = "cppl.law.invalid-clause-order";

// Refinement errors
inline constexpr std::string_view refinement_missing_where = "cppl.refinement.missing-where";
inline constexpr std::string_view refinement_missing_predicate = "cppl.refinement.missing-predicate";
inline constexpr std::string_view refinement_invalid_base = "cppl.refinement.invalid-base";

// Deprecated syntax
inline constexpr std::string_view deprecated_syntax = "cppl.deprecated.syntax";
inline constexpr std::string_view deprecated_keyword = "cppl.deprecated.keyword";

} // namespace diagnostic_codes

} // namespace cppl::lsp
