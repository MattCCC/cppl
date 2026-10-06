#pragma once

// What the files of the formatter share: the regions it lays out and the
// functions that find, lay out and replace them (format.cpp drives them).

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/formatter/format.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppl::formatter::detail {

// A declaration or loop header's clause block: the span to replace, the
// clauses to re-emit inside it in source order, and the column the whole
// declaration/loop starts at (clauses are indented one level past it, per the
// canonical rule).
struct ClauseRegion {
    source::ByteSpan span;
    std::vector<const frontend::Clause*> clauses;
    std::size_t declaration_column = 0;
    std::size_t declaration_offset = 0; // the declaration's own first byte, e.g. 'law'/'verified'
};

// Defined in format_clauses.cpp.
std::size_t line_start_column(std::string_view text, std::size_t offset);

std::size_t line_number(std::string_view text, std::size_t offset);

inline constexpr std::size_t kIndentWidth = 4; // .clang-format: IndentWidth 4

// Defined in format_clauses.cpp.
std::string_view keyword_spelling(frontend::ClauseKind kind);

// A predicate span's already-clang-formatted text, looked up by its original
// offset. Populated once per `format_ranges_once` call from a single batched
// clang-format invocation (`format_expression_spans`) over every clause/
// proof/refinement predicate in the document, so this layer relocates
// clauses and reads back Clang's own expression formatting rather than
// reimplementing operator spacing (AGENTS.md 14).
using PredicateText = std::unordered_map<std::size_t, std::string>;

// Defined in format_clauses.cpp.
bool has_cppl_only_syntax(std::string_view predicate);

std::string_view predicate_text(std::string_view text, source::ByteSpan span, const PredicateText& reformatted);

bool is_horizontal_or_newline(char c);

std::optional<source::ByteSpan> template_header_break(std::string_view text, std::size_t declaration_offset);

std::size_t layout_depth_at(const frontend::TokenStream& stream, std::size_t offset);

std::size_t split_column(const frontend::TokenStream& stream, const frontend::ProofStatement& statement);

std::vector<ClauseRegion> collect_regions(const frontend::TokenStream& stream, const frontend::Syntax& syntax);

// `proves (...)` is not a `Clause` (it is the fixed, single specification
// clause of a proof declaration, GRAMMAR.md 4), so it gets its own emission
// path rather than being folded into `canonical_clause_block`, which is
// written in terms of `Clause`.
struct ProofRegion {
    source::ByteSpan span;
    source::ByteSpan proposition;
    std::size_t declaration_column = 0;
    source::SourceLocation location; // the 'proves' keyword's presumed location
};

// Defined in format_clauses.cpp.
std::vector<ProofRegion> collect_proof_regions(const frontend::TokenStream& stream, const frontend::Syntax& syntax);

source::ByteSpan widen_over_whitespace(std::string_view text, source::ByteSpan span);

std::optional<FormatEdit> make_clause_edit(std::string_view text, const ClauseRegion& region,
                                           const PredicateText& reformatted);

std::optional<FormatEdit> make_proves_edit(std::string_view text, const ProofRegion& region,
                                           const PredicateText& reformatted);

bool spans_overlap(source::ByteSpan lhs, source::ByteSpan rhs);

// Proof-arm layout (GRAMMAR.md 5.7-5.9): `cases`/`decompose`/`induction`
// share one canonical arm block - `label(bindings) => { ... }`, one blank
// line between consecutive arms, nested arms indented one level deeper than
// their enclosing arm - collected the same way clause regions are: one
// region per statement with arms, a canonical replacement built from it, an
// edit only when that replacement differs from source.
struct ArmRegion {
    const frontend::ProofStatement* statement = nullptr;
    std::size_t declaration_column = 0; // the cases/decompose/induction keyword's own column
    // Where a statement following the block begins. It is the keyword's own
    // column, except after a split that is the unbraced body of a control
    // statement, whose next statement belongs to the control statement's level.
    std::optional<std::size_t> following_column = std::nullopt;
};

// Defined in format_arms.cpp.
void collect_arm_regions(const frontend::TokenStream& stream, const std::vector<frontend::ProofStatement>& statements,
                         std::vector<ArmRegion>& regions);

std::optional<FormatEdit> make_arm_edit(std::string_view text, const ArmRegion& region);

// Defined in format_clang.cpp.
std::vector<FormatEdit> format_ordinary_cpp_lines(std::string_view text,
                                                  const std::vector<std::pair<std::size_t, std::size_t>>& line_ranges,
                                                  const std::vector<source::ByteSpan>& excluded_spans,
                                                  const std::string& clang_format, const std::string& style_config,
                                                  const std::string& stem, std::vector<diagnostics::Diagnostic>& out);

std::vector<FormatEdit> format_expression_spans(std::string_view text, const std::vector<source::ByteSpan>& spans,
                                                const std::string& clang_format, const std::string& style_config,
                                                const std::string& stem, std::vector<diagnostics::Diagnostic>& out);

std::string apply_expression_edits(std::string_view text, source::ByteSpan span, const std::vector<FormatEdit>& edits);

} // namespace cppl::formatter::detail
