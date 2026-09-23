#pragma once

#include "cppl/clang/editor.hpp"
#include "cppl/lsp/document.hpp"
#include "cppl/lsp/editor_view.hpp"
#include "cppl/lsp/linter.hpp"
#include "cppl/lsp/protocol.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
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

    // Code actions over the same formatter engine: each syntax migration whose
    // edits touch `request.range`, as a quick fix, and canonical formatting of
    // the whole document as a fix-all. Formatting runs clang-format, so it is
    // computed only when asked for by kind or by the user, never as the cursor
    // moves.
    [[nodiscard]] std::vector<CodeAction> text_document_code_actions(const CodeActionRequest& request);

    // Proof-decomposition assistance.
    //
    // Both answer from the states the compiler's own case engine recorded for
    // this buffer (`Document::subject_states`), never from a decomposition
    // this server performed: there is one case engine and it is the
    // compiler's (`AGENTS.md` 39). Where the compiler has not confirmed a
    // subject's states -- it could not reach elaboration, or no provider
    // models the type -- these return nothing rather than guess.
    //
    // Ordinary C++ completion and hover are not attempted here; that is
    // clangd's job (`tools/cppl-lsp/README.md`).
    [[nodiscard]] std::vector<CompletionItem> text_document_completion(const TextDocumentIdentifier& id,
                                                                       const Position& position);
    [[nodiscard]] std::optional<Hover> text_document_hover(const TextDocumentIdentifier& id, const Position& position);

    // The proof-statement keywords a spelling-based grammar cannot color, as
    // encoded semantic tokens (semantic_tokens.hpp). `std::nullopt` means the
    // document is unknown.
    [[nodiscard]] std::optional<std::vector<std::uint32_t>> text_document_semantic_tokens(
        const TextDocumentIdentifier& id) const;

    // Where the name at `position` is defined, declared, typed or overridden,
    // as `destination` asks (LSP `textDocument/definition`, `declaration`,
    // `typeDefinition`, `implementation`). Clang answers, over the document's
    // projection, and only positions traced back to text an author wrote are
    // returned (tools/cppl-lsp/README.md, "Navigation"). `std::nullopt` means
    // the document is unknown.
    [[nodiscard]] std::optional<std::vector<Location>> text_document_navigate(clangbridge::Destination destination,
                                                                              const TextDocumentIdentifier& id,
                                                                              const Position& position);

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

    // The document's view, brought up to date with every open buffer first;
    // null when the document is unknown.
    EditorView* view_for(const std::string& uri);

    DocumentManager documents_;
    // One view per open document, each with the buffer generation it was last
    // refreshed at. Any edit to any open buffer can change what another
    // document's unit reads, so every edit starts a new generation.
    struct View {
        std::unique_ptr<EditorView> view;
        std::uint64_t generation = 0;
    };
    std::unordered_map<std::string, View> views_;
    std::uint64_t generation_ = 1;
    Linter linter_;
    DiagnosticPublisher diagnostic_publisher_;
    TextDocumentSyncKind sync_kind_ = TextDocumentSyncKind::Full;
    bool shutting_down_ = false;
    bool should_exit_ = false;
    std::string clang_;
    std::vector<std::string> clang_arguments_;
    // The Clang driver by path, for the editor units: Clang finds the standard
    // library and system headers relative to it.
    std::string driver_;
};

} // namespace cppl::lsp
