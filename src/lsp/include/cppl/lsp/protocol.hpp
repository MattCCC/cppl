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

} // namespace cppl::lsp
