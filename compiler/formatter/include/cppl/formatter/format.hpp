#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace cppl::formatter {

// One request to canonically format a C++L buffer.
//
// This is the single formatting engine shared by the `cppl-format` CLI and
// `cppl-lsp` (document/range/on-type formatting): both call the same
// `format_*` functions with the same request shape and get byte-identical
// output, the same way both the CLI and the LSP already share one compile
// pipeline (`cppl::driver::compile_buffer`, `cppl::driver::detail::run_pipeline`).
struct FormatRequest {
    std::string text;
    std::string clang_format; // the clang-format executable to shell out to
    std::string style_config; // path to a .clang-format file; empty uses the repo default
    std::string virtual_path; // used only for scratch file naming and diagnostics
};

// One replacement into `FormatRequest::text`, in ORIGINAL byte offsets.
struct FormatEdit {
    source::ByteSpan span;
    std::string replacement;
};

struct FormatResult {
    bool ok = false;
    // Non-overlapping, sorted by ascending offset. Empty means the requested
    // range(s) are already canonical.
    std::vector<FormatEdit> edits;
    std::vector<diagnostics::Diagnostic> diagnostics;
};

struct SyntaxFix {
    std::string title;
    std::vector<FormatEdit> edits;
};

// Explicit migration actions; compilation never applies these edits silently.
// Uses the same frontend recovery tree as formatting, with original byte spans.
[[nodiscard]] std::vector<SyntaxFix> syntax_fixes(const FormatRequest& request);

// The one range-scoped formatting core. `ranges` are byte spans in
// `request.text` to format; an empty vector means the whole document.
// `format_document`/`format_on_type` below are both thin wrappers around
// this: there is one engine, not a separate rule per LSP method
// (AGENTS.md 15: avoid duplicated implementations of one representation).
//
// A range that only partly touches a C++L clause block or an ordinary-C++
// line is expanded to that clause's/line's complete extent - an edit is never
// a partial token, and formatting a range never reaches into an unrelated
// region of the document (SPEC.md/GRAMMAR.md clause boundaries decide the
// expansion, never a regex or a heuristic guess).
[[nodiscard]] FormatResult format_ranges(const FormatRequest& request, const std::vector<source::ByteSpan>& ranges);

// Equivalent to `format_ranges(request, {})`.
[[nodiscard]] FormatResult format_document(const FormatRequest& request);

// The smallest safe formatting unit containing `position` (a byte offset):
// the enclosing C++L clause if `position` is inside one, otherwise the
// enclosing ordinary-C++ line. Returns no edits, rather than a larger region,
// when no safe unit can be determined - on-type formatting is conservative by
// construction (never reformats more than what was just typed).
[[nodiscard]] FormatResult format_on_type(const FormatRequest& request, std::size_t position,
                                          const std::string& trigger_character);

// The same clause-placement rule the engine enforces, reported as `Style`
// warnings instead of silently rewritten. This is cheap enough (no
// clang-format subprocess) to run on every keystroke for
// `textDocument/publishDiagnostics`.
[[nodiscard]] std::vector<diagnostics::Diagnostic> check_style(const frontend::TokenStream& stream,
                                                               const frontend::Syntax& syntax);

} // namespace cppl::formatter
