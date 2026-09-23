#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// The code-action kinds this server produces (LSP: `CodeActionKind`).
inline constexpr std::string_view kQuickFixKind = "quickfix";
inline constexpr std::string_view kFixAllKind = "source.fixAll.cppl";

struct CodeAction {
    std::string title;
    std::string kind;
    std::vector<TextEdit> edits;
};

// What a `textDocument/codeAction` request asks for (LSP: `CodeActionParams`).
struct CodeActionRequest {
    TextDocumentIdentifier document;
    Range range;
    // Kinds asked for (`context.only`); empty asks for every kind.
    std::vector<std::string> only;
    // The editor asked on its own, as the cursor moved, rather than because the
    // user did (`CodeActionTriggerKind.Automatic`).
    bool automatic = false;
};

struct FormattingOptions {
    std::uint32_t tabSize = 4;
    bool insertSpaces = true;
};

// LSP `CompletionItemKind`. A case label is an `EnumMember` when the
// representation spells it as one (a scoped enumerator) and a `Keyword` when the
// representation reserves the spelling (`valueless`, `none`, `unnamed`), which
// is the distinction `decomposition::LabelKind` already draws.
enum class CompletionItemKind : std::uint8_t {
    Text = 1,
    Method = 2,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Class = 7,
    Interface = 8,
    Module = 9,
    Property = 10,
    Unit = 11,
    Value = 12,
    Enum = 13,
    Keyword = 14,
    Snippet = 15,
    Color = 16,
    File = 17,
    Reference = 18,
    Folder = 19,
    EnumMember = 20,
    Constant = 21,
    Struct = 22,
    Event = 23,
    Operator = 24,
    TypeParameter = 25,
};

struct CompletionItem {
    std::string label;
    CompletionItemKind kind = CompletionItemKind::Keyword;
    // Shown beside the label: which representation supplies this state, or a
    // function's result type.
    std::string detail;
    std::string documentation;
    // What is inserted, when it differs from `label` (an arm skeleton with its
    // binders). Empty means insert `label`.
    std::string insertText;
    // Whether `insertText` is an LSP snippet, with `${1:placeholder}` tab stops.
    bool snippet = false;
    // What the editor filters by as the user types, when it differs from
    // `label`.
    std::string filterText;
    // Orders provider states ahead of anything the editor merges in, and keeps
    // the provider's own order (alternative<0> before alternative<1>) stable.
    std::string sortText;
    bool deprecated = false;
};

// A completion answer: its items, and whether typing more would change which
// items there are rather than only which of them match.
struct CompletionList {
    bool incomplete = false;
    std::vector<CompletionItem> items;
};

// One signature a call could be resolving to (LSP `SignatureInformation`), with
// each parameter as a byte range of `label`.
struct SignatureInformation {
    std::string label;
    std::string documentation;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> parameters;
};

struct SignatureHelp {
    std::vector<SignatureInformation> signatures;
    std::uint32_t active_signature = 0;
    std::uint32_t active_parameter = 0;
};

// What the client said it can do, where the answer changes what the server
// sends (LSP `ClientCapabilities`).
struct ClientCapabilities {
    // `textDocument.completion.completionItem.snippetSupport`.
    bool snippets = false;
    // `textDocument.documentSymbol.hierarchicalDocumentSymbolSupport`: the
    // outline may be nested, rather than a list naming each entry's container.
    bool hierarchical_symbols = false;
};

struct Hover {
    std::string contents;
    std::optional<Range> range;
};

// LSP `DocumentHighlightKind`.
enum class DocumentHighlightKind : std::uint8_t {
    Text = 1,
    Read = 2,
    Write = 3,
};

struct DocumentHighlight {
    Range range;
    DocumentHighlightKind kind = DocumentHighlightKind::Text;
};

// A line of text an editor shows above a range (LSP `CodeLens`), here only
// ever to state something: it runs no command.
struct CodeLens {
    Range range;
    std::string title;
};

// LSP `SymbolKind`.
enum class SymbolKind : std::uint8_t {
    File = 1,
    Module = 2,
    Namespace = 3,
    Package = 4,
    Class = 5,
    Method = 6,
    Property = 7,
    Field = 8,
    Constructor = 9,
    Enum = 10,
    Interface = 11,
    Function = 12,
    Variable = 13,
    Constant = 14,
    String = 15,
    Number = 16,
    Boolean = 17,
    Array = 18,
    Object = 19,
    Key = 20,
    Null = 21,
    EnumMember = 22,
    Struct = 23,
    Event = 24,
    Operator = 25,
    TypeParameter = 26,
};

// One declaration of a document's outline, with those nested in it (LSP
// `DocumentSymbol`). `selection` is its name, inside `range`, the whole
// declaration.
struct DocumentSymbol {
    std::string name;
    std::string detail;
    SymbolKind kind = SymbolKind::Function;
    Range range;
    Range selection;
    std::vector<DocumentSymbol> children;
};

} // namespace cppl::lsp
