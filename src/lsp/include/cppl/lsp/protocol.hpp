#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cppl::lsp {

// LSP position: line and character (both 0-indexed, character is UTF-16 code units)
struct Position {
    std::uint32_t line = 0;
    std::uint32_t character = 0;
};

// LSP range: half-open [start, end)
struct Range {
    Position start;
    Position end;
};

// LSP location: URI + range
struct Location {
    std::string uri;
    Range range;
};

enum class DiagnosticSeverity : std::uint8_t {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

struct DiagnosticRelatedInformation {
    Location location;
    std::string message;
};

struct Diagnostic {
    Range range;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string message;
    std::string source = "cppl";
    std::vector<DiagnosticRelatedInformation> relatedInformation;
};

enum class TextDocumentSyncKind : std::uint8_t {
    None = 0,
    Full = 1,
    Incremental = 2,
};

struct TextDocumentContentChangeEvent {
    std::optional<Range> range; // if not present, full document sync
    std::string text;
};

struct VersionedTextDocumentIdentifier {
    std::string uri;
    std::int32_t version = 0;
};

struct TextDocumentItem {
    std::string uri;
    std::string languageId;
    std::int32_t version = 0;
    std::string text;
};

struct TextDocumentIdentifier {
    std::string uri;
};

// A single replacement, as `textDocument/formatting` and friends respond with
// (LSP: `TextEdit`).
struct TextEdit {
    Range range;
    std::string newText;
};

struct CodeAction {
    std::string title;
    std::string kind;
    std::vector<TextEdit> edits;
};

struct FormattingOptions {
    std::uint32_t tabSize = 4;
    bool insertSpaces = true;
};

// The subset of LSP `CompletionItemKind` this server produces. A case label is
// an `EnumMember` when the representation spells it as one (a scoped
// enumerator) and a `Keyword` when the representation reserves the spelling
// (`valueless`, `none`, `unnamed`), which is the distinction
// `decomposition::LabelKind` already draws.
enum class CompletionItemKind : std::uint8_t {
    Keyword = 14,
    EnumMember = 20,
};

struct CompletionItem {
    std::string label;
    CompletionItemKind kind = CompletionItemKind::Keyword;
    // Shown beside the label: which representation supplies this state.
    std::string detail;
    std::string documentation;
    // What is inserted, when it differs from `label` (an arm skeleton with its
    // binders). Empty means insert `label`.
    std::string insertText;
    // Orders provider states ahead of anything the editor merges in, and keeps
    // the provider's own order (alternative<0> before alternative<1>) stable.
    std::string sortText;
};

struct Hover {
    std::string contents;
    std::optional<Range> range;
};

} // namespace cppl::lsp
