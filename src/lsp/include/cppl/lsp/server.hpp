#pragma once

#include "cppl/lsp/document.hpp"
#include "cppl/lsp/linter.hpp"
#include "cppl/lsp/protocol.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cppl::lsp {

// LSP server state and request handlers
class Server {
  public:
    // `clang` is the Clang driver executable used to preprocess and
    // semantically check buffers (mirrors `cppl::driver::Options::clang`).
    // `clang_arguments` are extra flags forwarded to Clang for every buffer
    // compile (include paths, defines, target flags); it may be empty.
    explicit Server(std::string clang = {}, std::vector<std::string> clang_arguments = {});

    // Lifecycle
    void initialize();
    void initialized();
    void shutdown();
    void exit();

    [[nodiscard]] bool is_shutting_down() const noexcept {
        return shutting_down_;
    }
    [[nodiscard]] bool should_exit() const noexcept {
        return should_exit_;
    }

    // Document synchronization
    void text_document_did_open(const TextDocumentItem& item);
    void text_document_did_change(const VersionedTextDocumentIdentifier& id,
                                  const std::vector<TextDocumentContentChangeEvent>& changes);
    void text_document_did_close(const TextDocumentIdentifier& id);

    // Formatting. All three share cppl::formatter's format_document/
    // format_ranges/format_on_type with the CLI (tools/cppl-format), so
    // canonical output is identical on both surfaces
    // (tools/cppl-lsp/README.md). `std::nullopt` means the document is
    // unknown; an empty (non-null) vector means it is known but already
    // canonical, matching how LSP distinguishes "no edits" from "no such
    // document" through a null vs. empty result.
    [[nodiscard]] std::optional<std::vector<TextEdit>> text_document_formatting(const TextDocumentIdentifier& id);
    [[nodiscard]] std::optional<std::vector<TextEdit>> text_document_range_formatting(const TextDocumentIdentifier& id,
                                                                                      const Range& range);
    [[nodiscard]] std::optional<std::vector<TextEdit>> text_document_on_type_formatting(
        const TextDocumentIdentifier& id, const Position& position, const std::string& trigger_character);

    // Diagnostics
    using DiagnosticPublisher = std::function<void(const std::string& uri, std::vector<Diagnostic>)>;
    void set_diagnostic_publisher(DiagnosticPublisher publisher) {
        diagnostic_publisher_ = std::move(publisher);
    }

    [[nodiscard]] TextDocumentSyncKind sync_kind() const noexcept {
        return sync_kind_;
    }

  private:
    void publish_diagnostics(const Document& doc);

    DocumentManager documents_;
    Linter linter_;
    DiagnosticPublisher diagnostic_publisher_;
    TextDocumentSyncKind sync_kind_ = TextDocumentSyncKind::Full;
    bool shutting_down_ = false;
    bool should_exit_ = false;
    std::string clang_;
    std::vector<std::string> clang_arguments_;
};

} // namespace cppl::lsp
